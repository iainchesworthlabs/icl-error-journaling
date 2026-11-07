// SPDX-License-Identifier: GPL-3.0-or-later
//
// The note draft over the real store (work package 2.6): the file journal, SQLite in WAL mode, and a timer the test
// advances by hand. The kill harness covers the draft under real kills (`ej_kill_driver`, "kill: a typed draft ...").
#include "error_journaling/draft_store.hpp"
#include "fake_draft_timer.hpp"
#include "recovery_fixture.hpp"

#include <chrono>

using namespace ej;
using namespace ej::test;
using namespace std::chrono_literals;

TEST_CASE("A flushed draft survives a clean reopen, and text still waiting for the timer does not", "[draft][sqlite]")
{
    TempDir dir;
    FakeDraftTimer timer;
    {
        OpenStore store(dir.path() / "store");
        DraftStore draft(store.repo(), store.clock(), timer);
        REQUIRE(draft.set("typed and flushed").has_value());
        REQUIRE(draft.flush().has_value());
        REQUIRE(draft.set("typed and flushed, then 200 ms more").has_value());
        timer.advance(200ms); // the write is due in 100 ms more
    }
    OpenStore reopened(dir.path() / "store");
    DraftStore draft(reopened.repo(), reopened.clock(), timer);
    const auto loaded = draft.load();
    REQUIRE(loaded.has_value());
    CHECK(*loaded == "typed and flushed"); // at most the delay's worth of typing is gone
}

TEST_CASE("The timer writes through to the database, and a cleared field removes the row", "[draft][sqlite]")
{
    TempDir dir;
    FakeDraftTimer timer;
    {
        OpenStore store(dir.path() / "store");
        DraftStore draft(store.repo(), store.clock(), timer);
        for (int i = 1; i <= 10; ++i)
        {
            REQUIRE(draft.set("take " + std::to_string(i)).has_value());
            timer.advance(20ms);
        }
        timer.advance(300ms);
        CHECK(store.repo().get_draft().value()->m_Text == "take 10");
    }
    {
        OpenStore store(dir.path() / "store");
        DraftStore draft(store.repo(), store.clock(), timer);
        CHECK(draft.load().value() == "take 10");
        REQUIRE(draft.set("").has_value());
        timer.advance(300ms);
        CHECK_FALSE(store.repo().get_draft().value().has_value());
    }
    OpenStore store(dir.path() / "store");
    CHECK_FALSE(store.repo().get_draft().value().has_value());
}

TEST_CASE("Committing a note clears the draft with the entry, and the store then has nothing to bring it back",
          "[draft][sqlite]")
{
    TempDir dir;
    FakeDraftTimer timer;
    OpenStore store(dir.path() / "store");
    REQUIRE(store.repo().create_log(log_id(1), sample_header()).has_value());
    DraftStore draft(store.repo(), store.clock(), timer);

    REQUIRE(draft.set("Played a song for Sam Example").has_value());
    timer.advance(300ms);
    REQUIRE(store.repo().get_draft().value().has_value());
    REQUIRE(draft.set("Played a song for Sam Example, twice").has_value()); // pending, not written

    REQUIRE(store.repo().add_entry(log_id(1), sample_note(1, 1, "Played a song for Sam Example, twice")).has_value());
    CHECK_FALSE(store.repo().get_draft().value().has_value()); // the same transaction
    draft.committed();
    timer.advance(5s);
    CHECK_FALSE(store.repo().get_draft().value().has_value());
    CHECK_FALSE(draft.has_pending());
    CHECK(store.repo().load_log(log_id(1)).value().m_Log.entries().size() == 1);

    // An error entry does not touch the draft.
    REQUIRE(draft.set("the next note").has_value());
    timer.advance(300ms);
    REQUIRE(store.repo().add_entry(log_id(1), sample_error(2, 2)).has_value());
    CHECK(store.repo().get_draft().value()->m_Text == "the next note");
}
