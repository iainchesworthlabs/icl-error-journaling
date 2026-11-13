// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_root.hpp"
#include "build_info.hpp"
#include "error_journaling/config_exporter.hpp"
#include "error_journaling/config_importer.hpp"
#include "error_journaling/launch_tracker.hpp"
#include "error_journaling/lifecycle.hpp"
#include "error_journaling/lifecycle_adapter.hpp"
#include "error_journaling/open_file_filter.hpp"
#include "error_journaling/platform_services.hpp"
#include "error_journaling/qml_bridge.hpp"
#include "error_journaling/setup_files.hpp"
#include "error_journaling/storage.hpp"
#include "error_journaling/version.hpp"
#include "qt_clipboard.hpp"
#include "viewmodel/about_model.hpp"
#include "viewmodel/board_state.hpp"
#include "viewmodel/help_state.hpp"
#include "viewmodel/interaction_board_model.hpp"
#include "viewmodel/settings_model.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <filesystem>
#include <string_view>
#include <utility>

namespace
{

void name_the_program()
{
    QCoreApplication::setApplicationName(QStringLiteral("field-test"));
    QCoreApplication::setApplicationVersion(error_journaling::version_text());
}

} // namespace

int main(int argc, char* argv[])
{
    // --help, --version and a mistake in the options need no window system, so they are dealt with first by an
    // application that has none.
    const QCommandLineOption smoke_test{QStringLiteral("smoke-test"),
                                        QStringLiteral("Exit after the first frame has been shown.")};
    const QCommandLineOption data_dir{QStringLiteral("data-dir"),
                                      QStringLiteral("Keep the data in this folder instead of the app's own."),
                                      QStringLiteral("folder")};
    {
        const QCoreApplication core(argc, argv);
        name_the_program();
        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("Field Test"));
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addOption(smoke_test);
        parser.addOption(data_dir);
        parser.process(core);
    }

    const QGuiApplication app(argc, argv);
    name_the_program();
    QCommandLineParser parser;
    parser.addOption(smoke_test);
    parser.addOption(data_dir);
    parser.parse(QCoreApplication::arguments());

    // What Qt does not cover on an iPhone: the share sheet and the haptics. The bridge owns them for QML (the Stage 0
    // spike controls); the services use the same two objects, which stay where they are when the bridge takes them.
    ej::bridge::PlatformServices services = ej::bridge::make_platform_services();
    ej::bridge::Share& share_sheet = *services.m_Share;
    ej::bridge::Haptics& haptics = *services.m_Haptics;
    // The marks the exit gate is measured by (NFR-04). The tracker reports the launch once, when the board is usable.
    ej::bridge::LaunchTracker launch(*services.m_PerfMarks);
    launch.process_started();
    // A file the system opens in the application (AirDrop, Files) arrives as an event, possibly before anything can
    // import it: the filter holds it until the importer is ready. What an earlier import left behind is removed now.
    ej::bridge::PerfMarks& marks = *services.m_PerfMarks;
    ej::bridge::DocumentPicker& document_picker = *services.m_DocumentPicker;
    ej::bridge::BackgroundTasks& background_tasks = *services.m_BackgroundTasks;
    ej::bridge::WindowLimits& window_limits = *services.m_WindowLimits;
    ej::bridge::OpenFileFilter open_files([&marks](std::string_view line) { marks.log(line); });
    QCoreApplication::instance()->installEventFilter(&open_files);
    const QString temporary_path = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    ej::bridge::purge_import_leftovers(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation),
                                       temporary_path);
    ej::bridge::QmlBridge platform_bridge(std::move(services));
    // M1 of the exit gate, readable on the phone (the timing readout) as well as in the log.
    launch.set_usable_handler([&platform_bridge](double ms) { platform_bridge.set_launch_ms(ms); });
    const bool share_available = share_sheet.available();

    // The composition root (Stage 3, 3.11): the store is opened and recovered, and the services are built and wired.
    // Declared before the engine, so the engine and the QML objects that use the models are destroyed first.
    ej::app::QtClipboard clipboard;
    ej::app::ProductionEnvironment production;
    ej::app::StoreOptions store;
    store.m_Clock = &production.clock();
    const QString data_path = parser.isSet(data_dir)
                                  ? parser.value(data_dir)
                                  : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    store.m_Root = std::filesystem::path(data_path.toStdU16String());
    ej::app::AppRoot root(production.environment(),
                          ej::app::AppPlatform{share_sheet, haptics, clipboard, share_available},
                          ej::app::start_sqlite_storage(store));
    // What the application does when it goes inactive and comes back (NOTE-03, DAY-01): flush the note draft and finish
    // the writes in flight inside a background task, and run the day check on becoming active once the store is ready.
    // Connected before the drain below, so the flush at quit comes first.
    ej::bridge::LifecycleActions lifecycle_actions;
    lifecycle_actions.flush_draft = [&root] { root.session()->flushDraft(); };
    lifecycle_actions.drain = [&root] { root.drain(); };
    lifecycle_actions.check_day = [&root] { root.day()->check(); };
    lifecycle_actions.set_application_active = [&platform_bridge](bool active)
    { platform_bridge.setApplicationActive(active); };
    lifecycle_actions.log = [&marks](std::string_view line) { marks.log(line); };
    ej::bridge::LifecycleCoordinator lifecycle(std::move(lifecycle_actions), background_tasks);
    ej::bridge::LifecycleAdapter lifecycle_adapter(
        *QCoreApplication::instance(), [&lifecycle](ej::bridge::LifecycleEvent event) { lifecycle.on_event(event); });
    // Every queued write is finished once, on the GUI thread, before the program ends.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &root, [&root] { root.drain(); });

    // Setup files: a file from the open-file event or the picker goes to the settings model, which reads and checks it;
    // the importer deletes the file when the model has answered, whatever the answer. The export writes a temporary
    // file and shares it. The Settings screens call these in Stage 4; until then only a file opened in the application
    // reaches them.
    ej::viewmodel::SettingsModel& settings = *root.settings();
    // The importer hands a file to the shell (SetupFiles::fileReady), which asks for confirmation and then calls
    // settings.loadImport; the importer deletes the file when the model has answered.
    ej::bridge::SetupFiles* setup_files_ptr = nullptr;
    ej::bridge::ConfigImporter importer([&setup_files_ptr](const QString& path) { setup_files_ptr->offer(path); });
    QObject::connect(&settings, &ej::viewmodel::SettingsModel::importPrepared, &importer,
                     [&importer] { importer.load_finished(); });
    QObject::connect(&settings, &ej::viewmodel::SettingsModel::importFailed, &importer,
                     [&importer] { importer.load_finished(); });
    QObject::connect(&open_files, &ej::bridge::OpenFileFilter::fileOpened, &importer,
                     [&importer](const QString& path) { importer.import_file(path); });
    QObject::connect(&importer, &ej::bridge::ConfigImporter::idle, &open_files,
                     [&open_files] { open_files.handled(); });
    ej::bridge::ConfigExporter exporter(
        ej::bridge::export_directory(temporary_path), [&settings](const QString& path) { settings.exportTo(path); },
        [&platform_bridge](const QString& path) { platform_bridge.shareTemporaryFile(path); });
    QObject::connect(&settings, &ej::viewmodel::SettingsModel::exportFinished, &exporter,
                     [&exporter](bool ok, const QString& message) { exporter.export_finished(ok, message); });
    ej::bridge::SetupFiles setup_files(importer, document_picker, exporter);
    setup_files_ptr = &setup_files;
    qmlRegisterSingletonInstance("ErrorJournalingApp", 1, 0, "SetupFiles", &setup_files);

    // The About pages (PRF-12): the facts of this build, which the build wrote (tools/licences/build_info.py), and the
    // third-party notices it generated from the real link line (tools/licences/notices.py, the resource below). The
    // notices are read when the About pages are first opened and not before, so they cost the launch nothing.
    const auto backend = error_journaling::storage::backend_name();
    const ej::app::BuildInfo build = ej::app::generated_build_info();
    const auto from_view = [](std::string_view text)
    { return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())); };
    ej::viewmodel::AboutFacts about_facts;
    about_facts.m_Name = QStringLiteral("Field Test");
    about_facts.m_Version = error_journaling::version_text();
    about_facts.m_BuildNumber = from_view(build.m_BuildNumber);
    about_facts.m_Commit = from_view(build.m_Commit);
    about_facts.m_Dirty = build.m_Dirty;
    about_facts.m_Ref = from_view(build.m_Ref);
    about_facts.m_BuildDate = from_view(build.m_BuildDate);
    about_facts.m_Platform = share_available ? QStringLiteral("ios") : QStringLiteral("desktop-stub");
    about_facts.m_Storage = from_view(std::string_view(backend.data(), backend.size()));
    about_facts.m_QtVersion = QString::fromLatin1(qVersion());
    about_facts.m_SourceUrl = from_view(build.m_SourceUrl);
    ej::viewmodel::AboutModel about(std::move(about_facts),
                                    []
                                    {
                                        QFile notices(QStringLiteral(":/about/third-party-notices.json"));
                                        return notices.open(QIODevice::ReadOnly) ? notices.readAll() : QByteArray();
                                    });

    // What the in-app help remembers: whether the first-run tip was closed, and on which days it was shown. Its own
    // small file beside the data, outside the store (Stage 10, 10.6).
    ej::viewmodel::HelpState help(QDir(data_path).filePath(QStringLiteral("help-state.ini")),
                                  [] { return QDate::currentDate(); });

    // Which boards there are (the optional second board is off until it is turned on in Settings) and which was used
    // last. Its own small file beside the data, outside the store (Stage 14, I-5). The board model is the stand-in that
    // I-6 replaces.
    ej::viewmodel::BoardState boards(QDir(data_path).filePath(QStringLiteral("board-state.ini")));
    ej::viewmodel::InteractionBoardModel interaction_board;

    QQmlApplicationEngine engine;
    QQmlEngine::setObjectOwnership(&about, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&help, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&boards, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(&interaction_board, QQmlEngine::CppOwnership);
    // The root owns the models; QML must never delete one.
    QObject* const models[] = {root.session(), root.undo(), root.board(),    root.picker(), root.timeline(),
                               root.history(), root.day(),  root.settings(), root.share()};
    for (QObject* model : models)
    {
        QQmlEngine::setObjectOwnership(model, QQmlEngine::CppOwnership);
    }
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    // The window reads the models as required properties (Stage 4, decision 3), and the root itself for its one
    // write-failed signal, which the shell shows as a banner. The version and platform text show the libraries are
    // linked.
    engine.setInitialProperties({{QStringLiteral("bridge"), QVariant::fromValue(&platform_bridge)},
                                 {QStringLiteral("versionText"), error_journaling::version_text()},
                                 {QStringLiteral("platformText"),
                                  QStringLiteral("%1, storage: %2")
                                      .arg(share_available ? QStringLiteral("ios") : QStringLiteral("desktop-stub"),
                                           QString::fromUtf8(backend.data(), static_cast<qsizetype>(backend.size())))},
                                 {QStringLiteral("session"), QVariant::fromValue(root.session())},
                                 {QStringLiteral("undo"), QVariant::fromValue(root.undo())},
                                 {QStringLiteral("board"), QVariant::fromValue(root.board())},
                                 {QStringLiteral("picker"), QVariant::fromValue(root.picker())},
                                 {QStringLiteral("timeline"), QVariant::fromValue(root.timeline())},
                                 {QStringLiteral("history"), QVariant::fromValue(root.history())},
                                 {QStringLiteral("day"), QVariant::fromValue(root.day())},
                                 {QStringLiteral("settings"), QVariant::fromValue(root.settings())},
                                 {QStringLiteral("share"), QVariant::fromValue(root.share())},
                                 {QStringLiteral("appRoot"), QVariant::fromValue(&root)},
                                 {QStringLiteral("about"), QVariant::fromValue(&about)},
                                 {QStringLiteral("help"), QVariant::fromValue(&help)},
                                 {QStringLiteral("boards"), QVariant::fromValue(&boards)},
                                 {QStringLiteral("interactionBoard"), QVariant::fromValue(&interaction_board)}});
    // The shell says when the 12 tiles accept taps (the Log tab is shown and the session has loaded).
    QObject::connect(&platform_bridge, &ej::bridge::QmlBridge::boardEnabled, &platform_bridge,
                     [&launch] { launch.board_enabled(); });
    engine.loadFromModule("ErrorJournalingApp", "Main");
    launch.qml_loaded();
    // The importer can take a file now; one that launched the application cold is delivered here.
    open_files.ready();
    // Recovery has finished: the store was opened above, before the window was made.
    launch.storage_ready();
    // The store was opened and recovered above: a day check that was waiting for it runs now. Qt may not send Active
    // for the launch itself (the state was already Active when the adapter connected), so the first one is sent here;
    // the check is idempotent, so a second one from Qt raises no second prompt.
    lifecycle.storage_ready();
    if (QGuiApplication::applicationState() == Qt::ApplicationActive)
    {
        lifecycle.on_event(ej::bridge::LifecycleEvent::BECAME_ACTIVE);
    }
    {
        const auto roots = engine.rootObjects();
        if (auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first()))
        {
            QObject::connect(window, &QQuickWindow::frameSwapped, &platform_bridge,
                             [&launch] { launch.frame_swapped(); });
            // The window declares the smallest size it is laid out for (Main.qml); a desktop honours that itself, and iPadOS
            // is told, now and each time the application becomes active (a window scene can connect after the start).
            const auto forward_minimum = [&window_limits, window]
            { window_limits.set_minimum_size(window->minimumWidth(), window->minimumHeight()); };
            forward_minimum();
            QObject::connect(&app, &QGuiApplication::applicationStateChanged, window,
                             [forward_minimum](Qt::ApplicationState state)
                             {
                                 if (state == Qt::ApplicationActive)
                                 {
                                     forward_minimum();
                                 }
                             });
        }
    }

    if (parser.isSet(smoke_test))
    {
        const auto roots = engine.rootObjects();
        auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first());
        if (window == nullptr)
        {
            return 1;
        }
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [] { QCoreApplication::quit(); });
    }

    return QGuiApplication::exec();
}
