// SPDX-License-Identifier: GPL-3.0-or-later
#include "app_root.hpp"
#include "error_journaling/launch_tracker.hpp"
#include "error_journaling/platform_services.hpp"
#include "error_journaling/qml_bridge.hpp"
#include "error_journaling/storage.hpp"
#include "error_journaling/version.hpp"
#include "qt_clipboard.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <filesystem>
#include <utility>

namespace
{

void name_the_program()
{
    QCoreApplication::setApplicationName(QStringLiteral("error-journaling"));
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
        parser.setApplicationDescription(QStringLiteral("CS Bingo Board"));
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
    ej::bridge::QmlBridge platform_bridge(std::move(services));
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
    // Every queued write is finished once, on the GUI thread, before the program ends.
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &root, [&root] { root.drain(); });

    QQmlApplicationEngine engine;
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
    // The window reads the models as required properties (Stage 4, decision 3). The placeholder also reads the version
    // and platform text, which shows the libraries are linked.
    const auto backend = error_journaling::storage::backend_name();
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
                                 {QStringLiteral("share"), QVariant::fromValue(root.share())}});
    engine.loadFromModule("ErrorJournalingApp", "Main");
    launch.qml_loaded();
    // Recovery has finished: the store was opened above, before the window was made. The shell of Stage 5, 5.7 calls
    // board_enabled() when the 12 tiles accept taps (the session is open); until then this line stands for it.
    launch.storage_ready();
    launch.board_enabled();
    {
        const auto roots = engine.rootObjects();
        if (auto* window = roots.isEmpty() ? nullptr : qobject_cast<QQuickWindow*>(roots.first()))
        {
            QObject::connect(window, &QQuickWindow::frameSwapped, &platform_bridge,
                             [&launch] { launch.frame_swapped(); });
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
