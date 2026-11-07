// SPDX-License-Identifier: GPL-3.0-or-later
//
// The note draft (work package 2.6, section 2.8 of docs/plan/stage-2.md), on the in-memory repository and a timer the
// test advances by hand: nothing here sleeps.
#include "error_journaling/draft_store.hpp"
#include "fake_draft_timer.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace std::chrono_literals;

namespace
{

struct Fixture
{
    CountingClock m_Clock{sample_time(100)};
    FakeDraftTimer m_Timer;
    DraftFailingRepository m_Repository{m_Clock};
    std::vector<StorageError> m_Errors;
    DraftStore m_Store{m_Repository, m_Clock, m_Timer,
                       DraftStoreOptions{[this](const StorageError& error) { m_Errors.push_back(error); }}};

    [[nodiscard]] std::optional<std::string> stored()
    {
        const auto draft = m_Repository.get_draft().value();
        return draft ? std::optional<std::string>(draft->m_Text) : std::nullopt;
    }
};

std::string typed(int n)
{
    return "the robot said take " + std::to_string(n) + " to Sam Example";
}

} // namespace

TEST_CASE("Twenty changes within 300 ms make one write, of the last text", "[draft]")
{
    Fixture f;
    for (int i = 1; i <= 20; ++i)
    {
        REQUIRE(f.m_Store.set(typed(i)).has_value());
        f.m_Timer.advance(10ms);
    }
    CHECK(f.m_Clock.reads() == 0);
    CHECK_FALSE(f.stored().has_value());
    f.m_Timer.advance(100ms); // 300 ms from the first change
    CHECK(f.m_Clock.reads() == 1);
    CHECK(f.m_Repository.m_DraftWrites == 1);
    CHECK(f.stored() == typed(20));
    CHECK_FALSE(f.m_Store.has_pending());
    f.m_Timer.advance(5s);
    CHECK(f.m_Repository.m_DraftWrites == 1);
}

TEST_CASE("A change at 299 ms is covered by the first write and one at 301 ms makes the next", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Store.set("first").has_value());
    f.m_Timer.advance(299ms);
    REQUIRE(f.m_Store.set("second").has_value());
    CHECK(f.m_Repository.m_DraftWrites == 0);
    f.m_Timer.advance(1ms); // 300 ms
    CHECK(f.m_Repository.m_DraftWrites == 1);
    CHECK(f.stored() == "second");
    f.m_Timer.advance(1ms); // 301 ms
    REQUIRE(f.m_Store.set("third").has_value());
    f.m_Timer.advance(299ms);
    CHECK(f.m_Repository.m_DraftWrites == 1);
    f.m_Timer.advance(1ms);
    CHECK(f.m_Repository.m_DraftWrites == 2);
    CHECK(f.stored() == "third");
}

TEST_CASE("Typing without a pause is still written every 300 ms, so a kill loses at most 300 ms of it", "[draft]")
{
    Fixture f;
    int written_at = -1; // the change whose text is in the row
    for (int ms = 0; ms < 3000; ms += 50)
    {
        REQUIRE(f.m_Store.set(typed(ms)).has_value());
        f.m_Timer.advance(50ms);
        for (int back = ms; back >= 0; back -= 50)
        {
            if (f.stored() == typed(back))
            {
                written_at = back;
                break;
            }
        }
        // 300 ms after a change its text, or a later one, is written.
        if (ms >= 350)
        {
            CHECK(written_at >= ms - 350);
        }
    }
    CHECK(f.m_Repository.m_DraftWrites >= 9);
    CHECK(f.m_Repository.m_DraftWrites <= 11);
}

TEST_CASE("flush writes at once and the timer then has nothing left to do", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Store.set("typed just before the app went inactive").has_value());
    f.m_Timer.advance(20ms);
    REQUIRE(f.m_Store.flush().has_value());
    CHECK(f.stored() == "typed just before the app went inactive");
    CHECK_FALSE(f.m_Timer.armed());
    f.m_Timer.advance(1s);
    CHECK(f.m_Repository.m_DraftWrites == 1);
    CHECK(f.m_Store.flush().has_value()); // nothing pending
    CHECK(f.m_Repository.m_DraftWrites == 1);
}

TEST_CASE("The wall clock stepping does not change when the write happens, only the time stored", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Store.set("one").has_value());
    f.m_Timer.advance(100ms);
    f.m_Clock.set(sample_time(100) - 24h * 10); // the wall clock steps back ten days
    REQUIRE(f.m_Store.set("two").has_value());
    f.m_Timer.advance(150ms);
    CHECK(f.m_Repository.m_DraftWrites == 0);    // 250 ms
    f.m_Clock.set(sample_time(100) + 24h * 400); // and forward more than a year
    f.m_Timer.advance(49ms);
    CHECK(f.m_Repository.m_DraftWrites == 0); // 299 ms
    f.m_Timer.advance(1ms);
    CHECK(f.m_Repository.m_DraftWrites == 1);
    CHECK(f.stored() == "two");
    CHECK(f.m_Repository.get_draft().value()->m_UpdatedUtc == sample_time(100) + 24h * 400);
}

TEST_CASE("An empty string clears the draft, and clearing an empty draft writes nothing", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Store.set("").has_value());
    CHECK_FALSE(f.m_Store.has_pending());
    CHECK_FALSE(f.m_Timer.armed());

    REQUIRE(f.m_Store.set("something").has_value());
    REQUIRE(f.m_Store.flush().has_value());
    REQUIRE(f.stored().has_value());
    REQUIRE(f.m_Store.set("").has_value());
    CHECK(f.m_Store.has_pending());
    f.m_Timer.advance(300ms);
    CHECK_FALSE(f.stored().has_value());
    CHECK_FALSE(f.m_Store.current().has_value());
    CHECK(f.m_Repository.m_DraftWrites == 2);
}

TEST_CASE("Setting the text back to what is stored cancels the write that was waiting", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Store.set("kept").has_value());
    REQUIRE(f.m_Store.flush().has_value());
    REQUIRE(f.m_Store.set("kept and more").has_value());
    REQUIRE(f.m_Store.set("kept").has_value());
    CHECK_FALSE(f.m_Store.has_pending());
    f.m_Timer.advance(1s);
    CHECK(f.m_Repository.m_DraftWrites == 1);
}

TEST_CASE("load tells the field what is stored, and the same text then writes nothing", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Repository.set_draft("from last time", sample_time(5)).has_value());
    const auto loaded = f.m_Store.load();
    REQUIRE(loaded.has_value());
    CHECK(*loaded == "from last time");
    CHECK(f.m_Store.current() == "from last time");
    const std::size_t writes_before = f.m_Repository.m_DraftWrites;
    REQUIRE(f.m_Store.set("from last time").has_value());
    f.m_Timer.advance(1s);
    CHECK(f.m_Repository.m_DraftWrites == writes_before);
}

TEST_CASE("Committing a note drops the pending draft, so a write cannot bring it back", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Repository.create_log(sample_id<LogId>(1), sample_header()).has_value());
    REQUIRE(f.m_Store.set("a note being typed").has_value());
    f.m_Timer.advance(300ms);
    REQUIRE(f.stored().has_value());
    REQUIRE(f.m_Store.set("a note being typed, a bit more").has_value());

    // The note is committed: the repository clears the draft in the same call, and the store forgets what it had.
    REQUIRE(
        f.m_Repository.add_entry(sample_id<LogId>(1), sample_note(1, 1, "a note being typed, a bit more")).has_value());
    f.m_Store.committed();
    CHECK_FALSE(f.m_Store.has_pending());
    CHECK_FALSE(f.m_Timer.armed());
    f.m_Timer.advance(5s);
    CHECK_FALSE(f.stored().has_value());
    CHECK(f.m_Repository.m_DraftWrites == 1);

    // Typing the next note starts again.
    REQUIRE(f.m_Store.set("the next one").has_value());
    f.m_Timer.advance(300ms);
    CHECK(f.stored() == "the next one");
}

TEST_CASE("A text that is not UTF-8 or is longer than a note is refused and nothing is kept", "[draft]")
{
    Fixture f;
    REQUIRE(f.m_Store.set("fine").has_value());
    const auto bad = f.m_Store.set(std::string("\xC3\x28 broken"));
    REQUIRE_FALSE(bad.has_value());
    CHECK(bad.error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);
    const auto long_text = f.m_Store.set(std::string(MAX_NOTE_CODE_POINTS + 1, 'a'));
    REQUIRE_FALSE(long_text.has_value());
    CHECK(long_text.error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(f.m_Store.current() == "fine");
    CHECK(f.m_Store.set(std::string(MAX_NOTE_CODE_POINTS, 'a')).has_value());
}

TEST_CASE("A write that fails keeps the text, reports the error, and is tried again", "[draft]")
{
    Fixture f;
    f.m_Repository.m_FailDraftWrites = true;
    REQUIRE(f.m_Store.set("typed while the disk was full").has_value());
    f.m_Timer.advance(300ms);
    REQUIRE(f.m_Errors.size() == 1);
    CHECK(f.m_Errors.front().m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(f.m_Store.has_pending());
    CHECK(f.m_Timer.armed());
    f.m_Timer.advance(300ms);
    CHECK(f.m_Errors.size() == 2);

    f.m_Repository.m_FailDraftWrites = false;
    f.m_Timer.advance(300ms);
    CHECK(f.m_Errors.size() == 2);
    CHECK_FALSE(f.m_Store.has_pending());
    CHECK(f.stored() == "typed while the disk was full");

    // A flush that fails returns the error, and keeps the text.
    f.m_Repository.m_FailDraftWrites = true;
    REQUIRE(f.m_Store.set("and again").has_value());
    const auto flushed = f.m_Store.flush();
    REQUIRE_FALSE(flushed.has_value());
    CHECK(flushed.error().m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(f.m_Store.has_pending());
    f.m_Repository.m_FailDraftWrites = false;
    CHECK(f.m_Store.flush().has_value());
    CHECK(f.stored() == "and again");
}

TEST_CASE("A store that goes away writes nothing and leaves no timer behind", "[draft]")
{
    CountingClock clock(sample_time(1));
    FakeDraftTimer timer;
    DraftFailingRepository repository(clock);
    {
        DraftStore store(repository, clock, timer);
        REQUIRE(store.set("not flushed").has_value());
        CHECK(timer.armed());
    }
    CHECK_FALSE(timer.armed());
    timer.advance(1s);
    CHECK(repository.m_DraftWrites == 0);
}
