// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/recording_repository.hpp"
#include "viewmodel/undo_controller.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QSignalSpy>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace std::chrono_literals;
using viewmodel::LogChange;
using viewmodel::LogDocument;
using viewmodel::UndoController;
using Origin = LogChange::Origin;

namespace
{

/// The minute offset of `sample_time` that reads as `hour:minute` in Melbourne (+11:00) on 7 November.
std::int64_t melbourne(int hour, int minute)
{
    return hour * 60 + minute - 11 * 60;
}

EntryId entry_id(std::uint64_t number)
{
    return sample_id<EntryId>(number);
}

Entry tap(std::uint64_t number, std::int64_t minute, std::uint64_t type = 1, std::string label = "Wrong Response")
{
    Entry entry = sample_error(number, minute, std::move(label));
    std::get<ErrorEntry>(entry.m_Body).m_ErrorType = sample_id<ErrorTypeId>(type);
    return entry;
}

/// "Wrong Response logged x2 at 10:20", with the multiplication sign as UTF-8.
std::string repeated(const std::string& name, int count, const std::string& time)
{
    return name + " logged \xC3\x97" + std::to_string(count) + " at " + time;
}

/// The position of the first event of this kind in the rig's `EventLog` whose detail is `detail` (any detail when
/// `detail` is empty); the size of the log when there is none, so a missing event never compares as early.
std::size_t first_index(DocumentRig& rig, EventKind kind, const std::string& detail)
{
    const std::vector<Event> events = rig.events().events();
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        if (events[i].m_Kind == kind && (detail.empty() || events[i].m_Detail == detail))
        {
            return i;
        }
    }
    return events.size();
}

/// An open log, the controller watching it and the clock they share. The controller is declared after the document, so
/// it goes first.
class UndoRig : public DocumentRig
{
public:
    UndoRig()
    {
        Configuration configuration = sample_configuration();
        configuration.m_TimeFormat = TimeFormat{TimeStyle::H24, false, MeridiemCase::UPPER};
        m_Source.set(std::move(configuration));
        controller.observe(*document);
        connect_events();
    }

    ~UndoRig()
    {
        controller.stop_observing(*document);
    }

    /// Submits a tap and commits it. Toasts open as the commit finishes.
    void commit(Entry entry, Origin origin = Origin::USER)
    {
        REQUIRE(document->add_entry(std::move(entry), origin, count(m_Done)));
        gateway().run_all();
    }

    void advance(std::chrono::milliseconds by)
    {
        world().m_Clock.advance(by);
    }

    [[nodiscard]] std::string text() const
    {
        return controller.text().toStdString();
    }

    [[nodiscard]] bool deleted(std::uint64_t number) const
    {
        const Entry* entry = document->log().find(entry_id(number));
        REQUIRE(entry != nullptr);
        return entry->m_Deleted;
    }

    [[nodiscard]] bool deleted_in_store(std::uint64_t number)
    {
        for (const Entry& entry : stored().m_Log.entries())
        {
            if (entry.m_Id == entry_id(number))
            {
                return entry.m_Deleted;
            }
        }
        FAIL("the entry is not in the store");
        return false;
    }

    [[nodiscard]] std::size_t sets_deleted()
    {
        return storage().repository().call_count(RepositoryOp::SET_ENTRY_DELETED);
    }

    [[nodiscard]] std::size_t toasts_opened() const
    {
        return m_Opened;
    }

    FakeConfigurationSource m_Source;
    UndoController controller{world().environment(), m_Source};
    QSignalSpy changed{&controller, &UndoController::toastChanged};
    QSignalSpy failed{&controller, &UndoController::writeFailed};
    int m_Done = 0;

private:
    /// Writes the toast into the `EventLog`, so that its place among the commits shows.
    void connect_events()
    {
        QObject::connect(&controller, &UndoController::toastChanged, &controller,
                         [this]
                         {
                             const bool opened = controller.active() && controller.generation() != m_Generation;
                             m_Generation = controller.generation();
                             events().record(EventKind::TOAST,
                                             opened ? "open" : (controller.active() ? "busy" : "closed"));
                             if (opened)
                             {
                                 ++m_Opened;
                             }
                         });
    }

    std::size_t m_Opened = 0;
    int m_Generation = 0;
};

} // namespace

// ------------------------------------------------------------------------------------------------------ the window

TEST_CASE("A tap opens the toast after the commit, and not before", "[viewmodel][undo]")
{
    UndoRig rig;

    REQUIRE(rig.document->add_entry(tap(1, melbourne(10, 20)), Origin::USER, rig.count(rig.m_Done)));
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.changed.count() == 0);
    CHECK(rig.controller.text().isEmpty());

    rig.gateway().run_all();
    CHECK(rig.controller.active());
    CHECK(rig.text() == "Wrong Response logged at 10:20");
    CHECK(rig.controller.repeat_count() == 1);
    CHECK(rig.controller.window_ms() == 5000);
    CHECK(first_index(rig, EventKind::COMMIT, "") < first_index(rig, EventKind::TOAST, "open"));
}

TEST_CASE("The properties are the ones QML reads", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));

    CHECK(rig.controller.property("active").toBool());
    CHECK(rig.controller.property("text").toString() == QString::fromStdString(rig.text()));
    CHECK(rig.controller.property("repeatCount").toInt() == 1);
    CHECK_FALSE(rig.controller.property("undoing").toBool());
    CHECK(rig.controller.property("generation").toInt() == 1);
    CHECK(rig.controller.property("windowMs").toInt() == 5000);
}

TEST_CASE("Undo at 4,999 ms removes the entry; at 5,000 ms the toast has closed", "[viewmodel][undo]")
{
    SECTION("inside the window")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20)));

        rig.advance(4999ms);
        REQUIRE(rig.controller.active());
        REQUIRE(rig.controller.undo());
        rig.gateway().run_all();

        CHECK(rig.deleted(1));
        CHECK(rig.deleted_in_store(1));
        CHECK_FALSE(rig.controller.active());
    }
    SECTION("at the end of the window")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20)));

        rig.advance(4999ms);
        rig.advance(1ms);
        CHECK_FALSE(rig.controller.active());
        CHECK_FALSE(rig.controller.undo());
        CHECK(rig.gateway().pending() == 0);
        CHECK_FALSE(rig.deleted(1));
    }
}

TEST_CASE("The window starts at the commit, not at the tap", "[viewmodel][undo]")
{
    UndoRig rig;
    REQUIRE(rig.document->add_entry(tap(1, melbourne(10, 20)), Origin::USER, rig.count(rig.m_Done)));

    rig.advance(300ms); // the commit is slow
    rig.gateway().run_all();
    REQUIRE(rig.controller.active());

    rig.advance(4999ms);
    CHECK(rig.controller.active());
    rig.advance(1ms);
    CHECK_FALSE(rig.controller.active());
}

TEST_CASE("A step of the wall clock changes nothing inside the window", "[viewmodel][undo]")
{
    SECTION("an hour back")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20)));
        rig.advance(2000ms);
        rig.world().m_Clock.step_wall_back(1h);
        rig.advance(2999ms);
        CHECK(rig.controller.active());
        rig.advance(1ms);
        CHECK_FALSE(rig.controller.active());
    }
    SECTION("an hour forward")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20)));
        rig.advance(2000ms);
        rig.world().m_Clock.step_wall_forward(1h);
        CHECK(rig.controller.active());
        REQUIRE(rig.controller.undo());
        rig.gateway().run_all();
        CHECK(rig.deleted(1));
    }
}

TEST_CASE("Each action restarts the window", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    const int first = rig.controller.generation();

    rig.advance(4000ms);
    rig.commit(tap(2, melbourne(10, 21)));
    CHECK(rig.controller.generation() == first + 1);
    CHECK(rig.world().m_Clock.pending_timers() == 1); // the first timer was cancelled

    rig.advance(4999ms); // 8,999 ms after the first tap
    CHECK(rig.controller.active());
    rig.advance(1ms);
    CHECK_FALSE(rig.controller.active());
}

// ---------------------------------------------------------------------------------------------------- the repeat

TEST_CASE("A repeat of the same tile counts while the toast shows, and the window slides", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.advance(1000ms);
    rig.commit(tap(2, melbourne(10, 21)));
    CHECK(rig.text() == repeated("Wrong Response", 2, "10:21"));
    CHECK(rig.controller.repeat_count() == 2);

    rig.advance(4900ms); // 5.9 s after the first, 4.9 s after the second
    rig.commit(tap(3, melbourne(10, 22)));
    CHECK(rig.text() == repeated("Wrong Response", 3, "10:22"));
    CHECK(rig.controller.repeat_count() == 3);

    rig.advance(5000ms); // idle
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.controller.repeat_count() == 1);
    rig.commit(tap(4, melbourne(10, 23)));
    CHECK(rig.text() == "Wrong Response logged at 10:23");
    CHECK(rig.controller.repeat_count() == 1);
}

TEST_CASE("Another tile takes the slot, and the earlier tap can no longer be undone", "[viewmodel][undo]")
{
    SECTION("A then B")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20), 1, "Wrong Response"));
        rig.commit(tap(2, melbourne(10, 21), 2, "Mute Delay"));
        CHECK(rig.text() == "Mute Delay logged at 10:21");
        CHECK(rig.controller.repeat_count() == 1);

        REQUIRE(rig.controller.undo());
        rig.gateway().run_all();
        CHECK(rig.deleted(2));
        CHECK_FALSE(rig.deleted(1));
        CHECK_FALSE(rig.controller.active());
        CHECK_FALSE(rig.controller.undo()); // A stays committed
    }
    SECTION("A, B, A is not a repeat")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20), 1, "Wrong Response"));
        rig.commit(tap(2, melbourne(10, 21), 2, "Mute Delay"));
        rig.commit(tap(3, melbourne(10, 22), 1, "Wrong Response"));
        CHECK(rig.text() == "Wrong Response logged at 10:22");
        CHECK(rig.controller.repeat_count() == 1);
    }
}

TEST_CASE("A note between two taps of one tile resets the count", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.commit(tap(2, melbourne(10, 21)));
    REQUIRE(rig.controller.repeat_count() == 2);

    rig.commit(sample_note(3, melbourne(10, 22), "Played Morning Medley"));
    CHECK(rig.text() == "Note added at 10:22");
    rig.commit(tap(4, melbourne(10, 23)));
    CHECK(rig.text() == "Wrong Response logged at 10:23");
    CHECK(rig.controller.repeat_count() == 1);
}

TEST_CASE("The same tile in two documents is not a repeat", "[viewmodel][undo]")
{
    UndoRig rig;
    auto other = std::make_shared<LogDocument>(rig.world().environment(), rig.gateway(), rig.stored());
    rig.controller.observe(*other);

    rig.commit(tap(1, melbourne(10, 20)));
    REQUIRE(other->add_entry(tap(2, melbourne(10, 21)), Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();

    CHECK(rig.text() == "Wrong Response logged at 10:21");
    CHECK(rig.controller.repeat_count() == 1);

    // The undo goes to the document of the latest tap.
    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(other->log().find(entry_id(2))->m_Deleted);
    CHECK_FALSE(rig.document->log().find(entry_id(1))->m_Deleted);

    rig.controller.stop_observing(*other);
}

TEST_CASE("Undo of a repeat removes only the most recent entry and closes the toast", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.commit(tap(2, melbourne(10, 21)));
    rig.commit(tap(3, melbourne(10, 22)));
    REQUIRE(rig.controller.repeat_count() == 3);

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();

    CHECK(rig.deleted(3));
    CHECK_FALSE(rig.deleted(1));
    CHECK_FALSE(rig.deleted(2));
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.controller.repeat_count() == 1);

    rig.commit(tap(4, melbourne(10, 23)));
    CHECK(rig.text() == "Wrong Response logged at 10:23");
    CHECK(rig.controller.repeat_count() == 1);
}

TEST_CASE("Taps queued behind a slow commit count when their own commit completes, in order", "[viewmodel][undo]")
{
    UndoRig rig;
    REQUIRE(rig.document->add_entry(tap(1, melbourne(10, 20)), Origin::USER, rig.count(rig.m_Done)));
    REQUIRE(rig.document->add_entry(tap(2, melbourne(10, 21)), Origin::USER, rig.count(rig.m_Done)));
    CHECK_FALSE(rig.controller.active());

    REQUIRE(rig.gateway().run_next());
    CHECK(rig.text() == "Wrong Response logged at 10:20");
    CHECK(rig.controller.repeat_count() == 1);

    REQUIRE(rig.gateway().run_next());
    CHECK(rig.text() == repeated("Wrong Response", 2, "10:21"));
    CHECK(rig.controller.repeat_count() == 2);
}

// ---------------------------------------------------------------------------------------------------------- undo

TEST_CASE("Undo pressed twice quickly commits once", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));

    REQUIRE(rig.controller.undo());
    CHECK(rig.controller.undoing());
    CHECK(rig.controller.active());
    CHECK_FALSE(rig.controller.undo());
    CHECK(rig.gateway().pending() == 1);

    rig.gateway().run_all();
    CHECK(rig.sets_deleted() == 1);
    CHECK_FALSE(rig.controller.undoing());
    CHECK_FALSE(rig.controller.active());
}

TEST_CASE("Undo racing the end of the window commits or does nothing, never both", "[viewmodel][undo]")
{
    SECTION("pressed before: it commits although the window ends before the commit")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20)));
        rig.advance(4999ms);
        REQUIRE(rig.controller.undo());
        rig.advance(1ms); // the window ends with the undo in flight
        CHECK_FALSE(rig.controller.active());
        rig.gateway().run_all();

        CHECK(rig.deleted(1));
        CHECK(rig.sets_deleted() == 1);
        CHECK_FALSE(rig.controller.active());
        CHECK_FALSE(rig.controller.undo());
    }
    SECTION("pressed after: nothing is queued")
    {
        UndoRig rig;
        rig.commit(tap(1, melbourne(10, 20)));
        rig.advance(5000ms);
        CHECK_FALSE(rig.controller.undo());
        rig.gateway().run_all();

        CHECK(rig.sets_deleted() == 0);
        CHECK_FALSE(rig.deleted(1));
    }
}

TEST_CASE("An undo whose commit fails keeps the toast and can be repeated", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    const int generation = rig.controller.generation();
    rig.advance(2000ms);
    rig.storage().repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);

    REQUIRE(rig.controller.undo());
    CHECK(rig.controller.undoing());
    rig.gateway().run_all();

    CHECK(rig.failed.count() == 1);
    CHECK(rig.failed.takeFirst().at(0).toString() == "undo");
    CHECK(rig.controller.active());
    CHECK_FALSE(rig.controller.undoing());
    CHECK(rig.controller.generation() == generation); // a failed press does not restart the window
    CHECK_FALSE(rig.deleted(1));
    CHECK_FALSE(rig.deleted_in_store(1));

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(rig.deleted(1));
    CHECK(rig.deleted_in_store(1));
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.failed.count() == 0);
}

TEST_CASE("After a failed undo the toast still ends with its window", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.advance(2000ms);
    rig.storage().repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);
    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();

    rig.advance(2999ms);
    CHECK(rig.controller.active());
    rig.advance(1ms);
    CHECK_FALSE(rig.controller.active());
    CHECK_FALSE(rig.controller.undo());
}

TEST_CASE("Dismiss closes the toast and the action stays committed", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));

    rig.controller.dismiss();
    CHECK_FALSE(rig.controller.active());
    CHECK_FALSE(rig.controller.undo());
    CHECK_FALSE(rig.deleted(1));
    CHECK(rig.world().m_Clock.pending_timers() == 0);
}

TEST_CASE("A later action replaces the toast while an undo is in flight, and the in-flight one still commits",
          "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    REQUIRE(rig.controller.undo());
    // The tester taps again before the undo has committed: queued behind it, so it opens its toast afterwards.
    REQUIRE(rig.document->add_entry(tap(2, melbourne(10, 21)), Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();

    CHECK(rig.deleted(1));
    CHECK_FALSE(rig.deleted(2));
    CHECK(rig.controller.active());
    CHECK(rig.text() == "Wrong Response logged at 10:21");
    CHECK_FALSE(rig.controller.undoing());
}

// ---------------------------------------------------------------------------------------------------- the wording

TEST_CASE("The line uses the time format and the zone of the entry", "[viewmodel][undo]")
{
    UndoRig rig;
    Configuration configuration = sample_configuration();
    configuration.m_TimeFormat = TimeFormat{TimeStyle::H12, true, MeridiemCase::LOWER};
    rig.m_Source.set(std::move(configuration));

    // 00:00:09 UTC is 08:00:09 in Perth (+08:00).
    Entry entry = tap(1, 0);
    entry.m_Captured = Timestamp{sample_time(0) + 9s, "Australia/Perth"};
    rig.commit(std::move(entry));
    CHECK(rig.text() == "Wrong Response logged at 08:00:09 am");

    // And 11:00:09 in Melbourne (+11:00), at 12-hour, for the same instant.
    Entry later = tap(2, 0);
    later.m_Captured = Timestamp{sample_time(60 * 3) + 9s, "Australia/Melbourne"};
    rig.commit(std::move(later));
    CHECK(rig.text() == repeated("Wrong Response", 2, "02:00:09 pm"));
}

TEST_CASE("The name is the snapshot taken when the entry was written", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20), 1, "Name As Written"));
    CHECK(rig.text() == "Name As Written logged at 10:20");
}

// ------------------------------------------------------------------------------------------------- notes, deletes

TEST_CASE("A note gets a toast, and its undo returns the text to the composer", "[viewmodel][undo]")
{
    UndoRig rig;
    std::vector<std::string> returned;
    rig.controller.set_note_undo_target([&returned](const std::string& text) { returned.push_back(text); });

    REQUIRE(rig.document->add_entry(sample_note(1, melbourne(10, 30), "Played Morning Medley"), Origin::USER,
                                    rig.count(rig.m_Done)));
    CHECK_FALSE(rig.controller.active());
    rig.gateway().run_all();
    CHECK(rig.text() == "Note added at 10:30");
    CHECK(rig.controller.repeat_count() == 1);

    rig.advance(4999ms);
    REQUIRE(rig.controller.undo());
    CHECK(returned.empty()); // only once the undo has committed
    rig.gateway().run_all();

    CHECK(rig.deleted(1));
    CHECK(rig.deleted_in_store(1));
    CHECK_FALSE(rig.controller.active());
    REQUIRE(returned.size() == 1);
    CHECK(returned[0] == "Played Morning Medley");
}

TEST_CASE("A note whose undo commit fails keeps its text for the retry", "[viewmodel][undo]")
{
    UndoRig rig;
    std::vector<std::string> returned;
    rig.controller.set_note_undo_target([&returned](const std::string& text) { returned.push_back(text); });
    rig.commit(sample_note(1, melbourne(10, 30), "Played Morning Medley"));
    rig.storage().repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(returned.empty());
    CHECK(rig.controller.active());

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    REQUIRE(returned.size() == 1);
    CHECK(returned[0] == "Played Morning Medley");
}

TEST_CASE("The undo of a tap does not touch the composer", "[viewmodel][undo]")
{
    UndoRig rig;
    int calls = 0;
    rig.controller.set_note_undo_target([&calls](const std::string&) { ++calls; });
    rig.commit(tap(1, melbourne(10, 20)));
    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(calls == 0);
}

TEST_CASE("A tap that a held tile gave a draft says so, and its undo gives the draft back", "[viewmodel][undo]")
{
    UndoRig rig;
    std::vector<std::string> returned;
    rig.controller.set_note_undo_target([&returned](const std::string& text) { returned.push_back(text); });
    rig.commit(tap(1, melbourne(10, 20)));
    CHECK(rig.text() == "Wrong Response logged at 10:20");
    const int generation = rig.controller.generation();

    rig.controller.note_attached(entry_id(1), "The robot paused twice");
    CHECK(rig.text() == "Wrong Response logged at 10:20 with note");
    // The window is the tap's: it does not restart.
    CHECK(rig.controller.generation() == generation);

    REQUIRE(rig.controller.undo());
    CHECK(returned.empty()); // only once the undo has committed
    rig.gateway().run_all();
    CHECK(rig.deleted(1));
    CHECK_FALSE(rig.controller.active());
    REQUIRE(returned.size() == 1);
    CHECK(returned[0] == "The robot paused twice");
}

TEST_CASE("A draft attached to another entry than the toast's is not the toast's to give back", "[viewmodel][undo]")
{
    UndoRig rig;
    std::vector<std::string> returned;
    rig.controller.set_note_undo_target([&returned](const std::string& text) { returned.push_back(text); });
    rig.commit(tap(1, melbourne(10, 20)));
    rig.commit(tap(2, melbourne(10, 21), 2));

    rig.controller.note_attached(entry_id(1), "The robot paused twice");
    CHECK(rig.text().find("with note") == std::string::npos);
    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(rig.deleted(2));
    CHECK(returned.empty());

    // No toast at all: nothing happens.
    rig.controller.note_attached(entry_id(1), "The robot paused twice");
    CHECK_FALSE(rig.controller.active());
}

TEST_CASE("A tap with a draft whose undo commit fails keeps the draft for the retry", "[viewmodel][undo]")
{
    UndoRig rig;
    std::vector<std::string> returned;
    rig.controller.set_note_undo_target([&returned](const std::string& text) { returned.push_back(text); });
    rig.commit(tap(1, melbourne(10, 20)));
    rig.controller.note_attached(entry_id(1), "The robot paused twice");
    rig.storage().repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(returned.empty());
    CHECK(rig.controller.active());
    CHECK(rig.text() == "Wrong Response logged at 10:20 with note");

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    REQUIRE(returned.size() == 1);
}

TEST_CASE("A later tap replaces a toast that has a draft, and its undo takes nothing to the composer", "[viewmodel][undo]")
{
    UndoRig rig;
    int calls = 0;
    rig.controller.set_note_undo_target([&calls](const std::string&) { ++calls; });
    rig.commit(tap(1, melbourne(10, 20)));
    rig.controller.note_attached(entry_id(1), "The robot paused twice");
    rig.commit(tap(2, melbourne(10, 21), 2));

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(calls == 0);
}

TEST_CASE("A delete from the review screen opens a toast and its undo restores the entry", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.advance(6s);
    REQUIRE_FALSE(rig.controller.active());

    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::USER, rig.count(rig.m_Done)));
    CHECK_FALSE(rig.controller.active()); // not before the commit
    rig.gateway().run_all();
    CHECK(rig.text() == "Entry deleted");
    CHECK(rig.deleted(1));

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK_FALSE(rig.deleted(1));
    CHECK_FALSE(rig.deleted_in_store(1));
    CHECK_FALSE(rig.controller.active());
}

TEST_CASE("After the window a review delete cannot be undone and the entry stays deleted", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.advance(6s);

    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    rig.advance(5000ms);

    CHECK_FALSE(rig.controller.active());
    CHECK_FALSE(rig.controller.undo());
    CHECK(rig.deleted(1));
    CHECK(rig.deleted_in_store(1));
}

TEST_CASE("A delete replaces a tap toast, which can then no longer be undone", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    rig.commit(tap(2, melbourne(10, 21), 2, "Mute Delay"));
    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();

    CHECK(rig.text() == "Entry deleted");
    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK_FALSE(rig.deleted(1)); // restored
    CHECK_FALSE(rig.deleted(2)); // Mute Delay stays
}

// ------------------------------------------------------------------------------------------- new log and archive

TEST_CASE("A new log closes the toast", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    REQUIRE(rig.controller.active());

    rig.document->replace(rig.stored());
    CHECK_FALSE(rig.controller.active());
    CHECK_FALSE(rig.controller.undo());
    CHECK(rig.world().m_Clock.pending_timers() == 0);
}

TEST_CASE("Undo of an edit to an archived log works the same way inside its window", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(9, 0)));
    rig.advance(6s);
    rig.document->set_archived();
    REQUIRE(rig.document->archived());

    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    REQUIRE(rig.controller.active());
    CHECK(rig.text() == "Entry deleted");

    rig.advance(4999ms);
    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK_FALSE(rig.deleted(1));
    CHECK_FALSE(rig.controller.active());

    // And a second delete, left to expire.
    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    rig.advance(5000ms);
    CHECK_FALSE(rig.controller.undo());
    CHECK(rig.deleted(1));
}

TEST_CASE("Leaving a document closes its toast", "[viewmodel][undo]")
{
    UndoRig rig;
    auto other = std::make_shared<LogDocument>(rig.world().environment(), rig.gateway(), rig.stored());
    rig.controller.observe(*other);
    REQUIRE(other->add_entry(tap(1, melbourne(10, 20)), Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    REQUIRE(rig.controller.active());

    rig.controller.stop_observing(*rig.document); // not the document of the toast
    CHECK(rig.controller.active());
    rig.controller.observe(*rig.document); // the rig stops observing it again

    rig.controller.stop_observing(*other);
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.world().m_Clock.pending_timers() == 0);
}

// ---------------------------------------------------------------------------------- no toast and no haptic

TEST_CASE("An undo never opens a toast and fires no haptic", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.commit(tap(1, melbourne(10, 20)));
    REQUIRE(rig.toasts_opened() == 1);

    REQUIRE(rig.controller.undo());
    rig.gateway().run_all();
    CHECK(rig.toasts_opened() == 1);
    CHECK_FALSE(rig.controller.active());

    // A restore with origin UNDO, made by anyone, opens nothing either.
    REQUIRE(rig.document->set_deleted(entry_id(1), false, Origin::UNDO, rig.count(rig.m_Done)));
    REQUIRE(rig.document->set_deleted(entry_id(1), true, Origin::UNDO, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    CHECK(rig.toasts_opened() == 1);
    CHECK_FALSE(rig.controller.active());

    CHECK(rig.events().count(EventKind::HAPTIC) == 0);
    CHECK(rig.events().count(EventKind::CONFIRMATION) == 0);
    // The commits in the log are the add and the deletes, and the only toast comes after the add.
    CHECK(first_index(rig, EventKind::COMMIT, "") < first_index(rig, EventKind::TOAST, "open"));
    CHECK(rig.events().count(EventKind::TOAST, "open") == 1);
}

TEST_CASE("A failed tap and a header change open no toast", "[viewmodel][undo]")
{
    UndoRig rig;
    rig.storage().repository().fail_next(RepositoryOp::ADD_ENTRY);
    REQUIRE(rig.document->add_entry(tap(1, melbourne(10, 20)), Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.changed.count() == 0);

    REQUIRE(rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0",
                                        Timestamp{sample_time(melbourne(10, 25)), "Australia/Melbourne"}, entry_id(2),
                                        rig.count(rig.m_Done)));
    rig.gateway().run_all();
    CHECK_FALSE(rig.controller.active());
    CHECK(rig.changed.count() == 0);
}

TEST_CASE("A controller that is gone is not called back by a late commit", "[viewmodel][undo]")
{
    UndoRig rig;
    auto gone = std::make_unique<UndoController>(rig.world().environment(), rig.m_Source);
    gone->observe(*rig.document);
    REQUIRE(rig.document->add_entry(tap(1, melbourne(10, 20)), Origin::USER, rig.count(rig.m_Done)));
    rig.gateway().run_all();
    REQUIRE(gone->active());
    REQUIRE(gone->undo());

    gone.reset(); // the undo is still waiting in the gateway
    rig.gateway().run_all();

    CHECK(rig.deleted(1));                            // it committed all the same
    CHECK(rig.world().m_Clock.pending_timers() == 1); // only the rig's own controller still has a timer
}
