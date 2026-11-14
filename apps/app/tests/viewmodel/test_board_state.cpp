// SPDX-License-Identifier: GPL-3.0-or-later
// BoardState (Stage 14, I-5): the second board is off on a fresh install and an upgrade, the last board is remembered,
// and turning the board off while on it goes back to Errors.
#include "viewmodel/board_state.hpp"
#include "viewmodel/interaction_board_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QSignalSpy>
#include <QString>
#include <QTemporaryDir>
#include <memory>

using namespace ej;
using viewmodel::BoardState;

namespace
{

struct Fixture
{
    QTemporaryDir m_Dir;

    QString file() const
    {
        return m_Dir.filePath(QStringLiteral("board-state.ini"));
    }

    std::unique_ptr<BoardState> open() const
    {
        return std::make_unique<BoardState>(file());
    }
};

} // namespace

TEST_CASE("shell.default_off: a fresh install has the second board off and opens on Errors", "[unit][shell]")
{
    const Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    const auto state = fixture.open();
    CHECK_FALSE(state->interactions_enabled());
    CHECK(state->last_board() == QStringLiteral("errors"));
    // An upgrade has no file either, and an empty path keeps nothing.
    CHECK_FALSE(BoardState(QString()).interactions_enabled());
}

TEST_CASE("shell.last_board: the board last used is what the app reopens on", "[unit][shell]")
{
    const Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    {
        const auto state = fixture.open();
        state->set_interactions_enabled(true);
        QSignalSpy changed(state.get(), &BoardState::lastBoardChanged);
        state->set_last_board(QStringLiteral("interactions"));
        CHECK(state->last_board() == QStringLiteral("interactions"));
        CHECK(changed.count() == 1);
        // The same board again says nothing more.
        state->set_last_board(QStringLiteral("interactions"));
        CHECK(changed.count() == 1);
    }
    const auto reopened = fixture.open();
    CHECK(reopened->interactions_enabled());
    CHECK(reopened->last_board() == QStringLiteral("interactions"));
}

TEST_CASE("shell.toggle: off while on the Interactions board goes to Errors, and the board is never last while off",
          "[unit][shell]")
{
    const Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    const auto state = fixture.open();
    // The second board cannot be the last board while it is off.
    state->set_last_board(QStringLiteral("interactions"));
    CHECK(state->last_board() == QStringLiteral("errors"));

    state->set_interactions_enabled(true);
    state->set_last_board(QStringLiteral("interactions"));
    QSignalSpy board(state.get(), &BoardState::lastBoardChanged);
    QSignalSpy enabled(state.get(), &BoardState::interactionsEnabledChanged);
    state->set_interactions_enabled(false);
    CHECK(state->last_board() == QStringLiteral("errors"));
    CHECK(board.count() == 1);
    CHECK(enabled.count() == 1);

    // What was saved says the same after a restart.
    const auto reopened = fixture.open();
    CHECK_FALSE(reopened->interactions_enabled());
    CHECK(reopened->last_board() == QStringLiteral("errors"));
}

TEST_CASE("shell.default_off: a damaged file is a v1 install", "[unit][shell]")
{
    const Fixture fixture;
    REQUIRE(fixture.m_Dir.isValid());
    QFile file(fixture.file());
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write("[boards]\ninteractionsEnabled=yes please\nlast=interactions\n");
    file.close();
    const auto state = fixture.open();
    CHECK_FALSE(state->interactions_enabled());
    CHECK(state->last_board() == QStringLiteral("errors"));
}

TEST_CASE("shell.empty_board: the stand-in board has no types", "[unit][shell]")
{
    const viewmodel::InteractionBoardModel model;
    CHECK(model.board_empty());
}
