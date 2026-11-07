// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/ios_bridge.hpp"
#include "error_journaling/storage.hpp"
#include "error_journaling/version.hpp"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>

namespace
{

void name_the_program()
{
    QCoreApplication::setApplicationName(QStringLiteral("error-journaling"));
    QCoreApplication::setApplicationVersion(error_journaling::version_text());
}

}  // namespace

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
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    // The placeholder reads these two through the initial properties, which shows the libraries are linked.
    engine.setInitialProperties(
        {{QStringLiteral("versionText"), error_journaling::version_text()},
         {QStringLiteral("platformText"),
          QStringLiteral("%1, storage: %2")
              .arg(QString::fromUtf8(error_journaling::bridge::implementation_name().data(),
                                     static_cast<qsizetype>(error_journaling::bridge::implementation_name().size())),
                   QString::fromUtf8(error_journaling::storage::backend_name().data(),
                                     static_cast<qsizetype>(error_journaling::storage::backend_name().size())))}});
    engine.loadFromModule("ErrorJournalingApp", "Main");

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
