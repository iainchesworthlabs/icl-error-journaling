// SPDX-License-Identifier: GPL-3.0-or-later
// HelpState (Stage 10, 10.6): the first-run tip is open until it is closed or has been shown on three days, and what it
// remembers survives a restart.
#include "viewmodel/help_state.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QDate>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <memory>

using namespace ej;
using viewmodel::HelpState;

namespace
{

/// A settings file in a directory of its own, and a date that the test moves.
struct Fixture
{
    QTemporaryDir m_Dir;
    QDate m_Today = QDate(2026, 11, 9);

    QString file() const
    {
        return m_Dir.filePath(QStringLiteral("help-state.ini"));
    }

    std::unique_ptr<HelpState> open()
    {
        return std::make_unique<HelpState>(file(), [this] { return m_Today; });
    }
};

} // namespace

TEST_CASE("help.state.tip_open: the tip is open on a first run and stays open until it is closed", "[unit][help]")
{
    Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    auto state = fixture.open();
    CHECK(state->tip_open());

    state->noteTipShown();
    CHECK(state->tip_open());

    QSignalSpy changed(state.get(), &HelpState::tipOpenChanged);
    state->closeTip();
    CHECK_FALSE(state->tip_open());
    CHECK(changed.count() == 1);

    // Closing again says nothing more.
    state->closeTip();
    CHECK(changed.count() == 1);
}

TEST_CASE("help.state.persist: a closed tip stays closed after a restart", "[unit][help]")
{
    Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    {
        auto state = fixture.open();
        state->closeTip();
    }
    auto again = fixture.open();
    CHECK_FALSE(again->tip_open());
}

TEST_CASE("help.state.days: the tip is shown on three days and not on a fourth", "[unit][help]")
{
    Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    for (int day = 0; day < HelpState::TIP_DAYS; ++day)
    {
        auto state = fixture.open();
        INFO("day " << day);
        CHECK(state->tip_open());
        // Shown twice on the same day is one day.
        state->noteTipShown();
        state->noteTipShown();
        fixture.m_Today = fixture.m_Today.addDays(1);
    }
    auto fourth = fixture.open();
    CHECK_FALSE(fourth->tip_open());
}

TEST_CASE("help.state.same_day: after its third day the tip stays for the rest of that day", "[unit][help]")
{
    Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    for (int day = 0; day < HelpState::TIP_DAYS - 1; ++day)
    {
        auto state = fixture.open();
        state->noteTipShown();
        fixture.m_Today = fixture.m_Today.addDays(1);
    }
    auto third = fixture.open();
    third->noteTipShown();
    CHECK(third->tip_open());
    // A restart on that day finds the tip as it was.
    auto restarted = fixture.open();
    CHECK(restarted->tip_open());
}

TEST_CASE(
    "help.state.damaged: a file that is not readable is a first run, and a file that cannot be written costs nothing",
    "[unit][help]")
{
    Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    QFile file(fixture.file());
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[firstRunTip]\nclosed=maybe\ndays=not-a-date, 2026-13-45, 2026-11-01, 2026-11-01\n");
    file.close();

    auto state = fixture.open();
    CHECK(state->tip_open());

    // No file at all: the state works in memory.
    HelpState nowhere(QString(), [&fixture] { return fixture.m_Today; });
    CHECK(nowhere.tip_open());
    nowhere.noteTipShown();
    nowhere.closeTip();
    CHECK_FALSE(nowhere.tip_open());
}
