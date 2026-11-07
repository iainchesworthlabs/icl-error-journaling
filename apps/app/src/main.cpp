// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/launch_tracker.hpp"
#include "error_journaling/qml_bridge.hpp"
#include "error_journaling/storage.hpp"
#include "error_journaling/version.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
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
    {
        const QCoreApplication core(argc, argv);
        name_the_program();
        QCommandLineParser parser;
        parser.setApplicationDescription(QStringLiteral("Error Journaling"));
        parser.addHelpOption();
        parser.addVersionOption();
        parser.addOption(smoke_test);
        parser.process(core);
    }

    const QGuiApplication app(argc, argv);
    name_the_program();
    QCommandLineParser parser;
    parser.addOption(smoke_test);
    parser.parse(QCoreApplication::arguments());

    QQmlApplicationEngine engine;
    // What Qt does not cover on an iPhone: the share sheet and the haptics (the Stage 0 spikes).
    ej::bridge::PlatformServices services = ej::bridge::make_platform_services();
    // The marks the exit gate is measured by (NFR-04). The tracker reports the launch once, when the board is usable.
    ej::bridge::LaunchTracker launch(*services.m_PerfMarks);
    launch.process_started();
    ej::bridge::QmlBridge platform_bridge(std::move(services));
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    // The placeholder reads these two through the initial properties, which shows the libraries are linked.
    engine.setInitialProperties(
        {{QStringLiteral("bridge"), QVariant::fromValue(&platform_bridge)},
         {QStringLiteral("versionText"), error_journaling::version_text()},
         {QStringLiteral("platformText"),
          QStringLiteral("%1, storage: %2")
              .arg(platform_bridge.share_available() ? QStringLiteral("ios") : QStringLiteral("desktop-stub"),
                   QString::fromUtf8(error_journaling::storage::backend_name().data(),
                                     static_cast<qsizetype>(error_journaling::storage::backend_name().size())))}});
    engine.loadFromModule("ErrorJournalingApp", "Main");
    launch.qml_loaded();
    // The application does not open storage yet, so the board has nothing to wait for; the shell that opens it (Stage
    // 5, 5.7) calls storage_ready() when recovery is done and board_enabled() when the 12 tiles accept taps, and these
    // two lines go.
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
