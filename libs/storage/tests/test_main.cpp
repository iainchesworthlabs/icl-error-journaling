// SPDX-License-Identifier: GPL-3.0-or-later
//
// Qt SQL loads its drivers through the application object, so the tests that open a database need one.
#include <catch2/catch_session.hpp>

#include <QCoreApplication>

int main(int argc, char* argv[])
{
    const QCoreApplication application(argc, argv);
    return Catch::Session().run(argc, argv);
}
