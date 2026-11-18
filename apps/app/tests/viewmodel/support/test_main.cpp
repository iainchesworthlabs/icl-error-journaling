// SPDX-License-Identifier: GPL-3.0-or-later
//
// The tests of Qt classes need an application object: queued calls, timers and QSignalSpy go through its event loop
//.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>

int main(int argc, char* argv[])
{
    const QCoreApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
