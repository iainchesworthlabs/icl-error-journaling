// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tests of the QML run a Qt Quick window without a screen: the offscreen platform and the software renderer, as
// `app.smoke` does (docs/plan/stage-4.md, decision 1). The variables are set here as well as by CTest, because
// `catch_discover_tests` starts the program once at build time to list the cases and that run has no CTest environment.
#include <catch2/catch_session.hpp>

#include <QGuiApplication>
#include <QtGlobal>

int main(int argc, char* argv[])
{
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
    {
        qputenv("QT_QPA_PLATFORM", "offscreen");
    }
    qputenv("QT_QUICK_BACKEND", "software");
    qputenv("QSG_RENDER_LOOP", "basic");
    // A screen reader is not running, but the accessibility tree is read through the same interface it would use.
    qputenv("QT_LINUX_ACCESSIBILITY_ALWAYS_ON", "1");

    const QGuiApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
