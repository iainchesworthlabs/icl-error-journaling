// SPDX-License-Identifier: GPL-3.0-or-later
//
// The threading tests of docs/plan/stage-3.md ("Threading tests"), against the application as `main.cpp` builds it: the
// real store on its own thread, behind the composition root. The gateway on its own (order, drain, shutdown) is in
// test_threaded_storage_gateway.cpp. These also run under ThreadSanitizer in CI.
#include "support/rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QString>
#include <QTimer>
#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace std::chrono_literals;

namespace
{

using Wall = std::chrono::steady_clock;

// The frame budget is one frame at 60 Hz (16 ms). The commit that is held off the GUI thread takes 40 ms, so a gap
// anywhere near that would show a blocked thread. The ceiling is a little above the frame on purpose: a loaded CI
// runner and ThreadSanitizer delay a timer by a few milliseconds, and the final budget is set from measurement on a
// device in Stage 5 (standard 6.5).
constexpr auto LONGEST_GAP = 30ms;
constexpr auto TAP_BUDGET_P95 = 5ms;

QString type_id_at(Rig& rig, int row)
{
    const int role = rig.board().roleNames().key(QByteArray("typeId"), -1);
    return rig.board().data(rig.board().index(row, 0), role).toString();
}

QString label_at(Rig& rig, int row)
{
    const int role = rig.board().roleNames().key(QByteArray("label"), -1);
    return rig.board().data(rig.board().index(row, 0), role).toString();
}

void import_and_open(Rig& rig)
{
    rig.import_sample_configuration();
    rig.settle();
    REQUIRE(rig.session().state() == viewmodel::LogSession::State::OPEN);
}

} // namespace

TEST_CASE("Taps during a slow commit do not block the GUI thread, and every entry arrives in tap order",
          "[viewmodel][threading][integration]")
{
    Rig rig(RigStore::SQLITE);
    import_and_open(rig);

    // Every commit takes 40 ms inside the store.
    rig.repository().set_commit_delay(40ms);

    std::vector<Wall::time_point> ticks;
    QTimer ticker;
    ticker.setInterval(4);
    ticker.setTimerType(Qt::PreciseTimer);
    QObject::connect(&ticker, &QTimer::timeout, &ticker, [&ticks] { ticks.push_back(Wall::now()); });
    ticker.start();

    constexpr int TAPS = 20;
    std::vector<std::chrono::nanoseconds> durations;
    std::vector<QString> expected_labels;
    for (int i = 0; i < TAPS; ++i)
    {
        const int row = i % 11;
        expected_labels.push_back(label_at(rig, row));
        const auto start = Wall::now();
        const bool accepted = rig.session().recordError(type_id_at(rig, row));
        durations.push_back(Wall::now() - start);
        REQUIRE(accepted);
        // The person's next tap comes a little later; the event loop runs in between, as it does in the app.
        rig.advance(1ms);
        const auto until = Wall::now() + 12ms;
        while (Wall::now() < until)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            std::this_thread::sleep_for(1ms);
        }
    }
    CHECK(rig.session().pending_writes() > 0); // the store is still behind: 20 commits of 40 ms
    rig.settle();
    ticker.stop();

    REQUIRE(ticks.size() > 20);
    std::chrono::nanoseconds longest{0};
    for (std::size_t i = 1; i < ticks.size(); ++i)
    {
        longest = std::max(longest, ticks[i] - ticks[i - 1]);
    }
    INFO("longest gap between ticks: " << std::chrono::duration_cast<std::chrono::microseconds>(longest).count()
                                       << " us");
    CHECK(longest < LONGEST_GAP);

    std::sort(durations.begin(), durations.end());
    const auto p95 = durations[static_cast<std::size_t>(durations.size() * 95 / 100)];
    INFO("recordError, 95th percentile: " << std::chrono::duration_cast<std::chrono::microseconds>(p95).count()
                                          << " us");
    CHECK(p95 < TAP_BUDGET_P95);

    // All 20 entries are there, in tap order.
    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    std::vector<QString> labels;
    for (const Entry& entry : stored->m_Log.entries())
    {
        if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body); error != nullptr)
        {
            labels.push_back(QString::fromStdString(error->m_ErrorTypeLabel));
        }
    }
    CHECK(labels == expected_labels);
    CHECK(rig.timeline().entry_count() == TAPS);
    CHECK(rig.board().total_count() == TAPS);
}

TEST_CASE("The store is used only on its own thread, never the GUI thread", "[viewmodel][threading][integration]")
{
    Rig rig(RigStore::SQLITE);
    import_and_open(rig);

    // A tap, a note, a header correction, a draft, a review edit, a share and a new log: every kind of call.
    rig.go_to(7, 10, 0);
    REQUIRE(rig.tap(type_id_at(rig, 0)));
    REQUIRE(rig.add_note(QStringLiteral("Played Morning Medley")));
    CHECK(rig.session().correctHeader(QStringLiteral("unit_number"), QStringLiteral("27")));
    rig.session().set_draft(QStringLiteral("a draft"));
    rig.advance(400ms);
    rig.settle();
    CHECK(rig.share().shareOpenLog());
    rig.settle();
    CHECK(rig.session().startNewLog());
    rig.settle();
    rig.history().refresh();
    rig.settle();

    // The calls on the repository, and a job run now, are all on one thread, and it is not this one.
    const auto storage_thread = rig.in_store([](viewmodel::StorageContext&) { return std::this_thread::get_id(); });
    CHECK(storage_thread != std::this_thread::get_id());
    const auto threads = rig.repository_threads();
    REQUIRE(threads.size() == 1);
    CHECK(*threads.begin() == storage_thread);
    CHECK(rig.mutating_calls() >= 6);
}

TEST_CASE("The first frame does not wait for the store: loading, with empty models, and then open",
          "[viewmodel][threading][integration]")
{
    Rig rig(RigStore::SQLITE);
    import_and_open(rig);
    REQUIRE(rig.tap(type_id_at(rig, 0)));
    rig.settle();
    REQUIRE(rig.timeline().entry_count() == 1);

    // A relaunch whose store has not answered yet.
    rig.hold_startup();
    rig.restart();
    rig.pump();
    CHECK(rig.session().state() == viewmodel::LogSession::State::LOADING);
    CHECK(rig.settings().loading());
    CHECK(rig.timeline().rowCount() == 0);
    CHECK(rig.timeline().entry_count() == 0);
    CHECK_FALSE(rig.timeline().ready());
    CHECK(rig.board().rowCount() == viewmodel::BOARD_ROWS);
    CHECK(rig.board().total_count() == 0);
    CHECK_FALSE(rig.undo().active());
    CHECK_FALSE(rig.day().prompt_visible());
    CHECK(rig.history().rowCount() == 0);
    // Taps are refused rather than lost while the log is not open.
    CHECK_FALSE(rig.session().recordError(type_id_at(rig, 0)));

    rig.release_startup();
    CHECK(rig.session().state() == viewmodel::LogSession::State::OPEN);
    CHECK(rig.timeline().entry_count() == 1);
    CHECK(rig.board().total_count() == 1);
}

TEST_CASE("Confirmation waits for the commit: no count, toast, Last bar, row or note haptic while it is held",
          "[viewmodel][threading][integration]")
{
    Rig rig(RigStore::SQLITE);
    import_and_open(rig);

    rig.hold_commits();
    REQUIRE(rig.tap(type_id_at(rig, 0)));
    REQUIRE(rig.add_note(QStringLiteral("Played Morning Medley")));
    rig.pump();
    // The touch-down impact has fired; nothing that says "saved" has.
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::IMPACT) == 1);
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::SUCCESS) == 0);
    CHECK(rig.board().total_count() == 0);
    CHECK_FALSE(rig.undo().active());
    CHECK(rig.timeline().last_line().isEmpty());
    CHECK(rig.timeline().rowCount() == 0);
    CHECK(rig.timeline().entry_count() == 0);
    CHECK(rig.session().pending_writes() == 2);

    rig.release_commits();
    CHECK(rig.board().total_count() == 1);
    CHECK(rig.undo().active());
    CHECK_FALSE(rig.timeline().last_line().isEmpty());
    CHECK(rig.timeline().rowCount() == 2);
    CHECK(rig.platform().haptic_count(ej::bridge::HapticKind::SUCCESS) == 1);

    // The success haptic of the note follows the commit of the note.
    const auto success = rig.events().first_index(EventKind::HAPTIC, "success");
    REQUIRE(success.has_value());
    std::optional<std::string> last_commit;
    const auto events = rig.events().events();
    for (std::size_t i = 0; i < *success; ++i)
    {
        if (events[i].m_Kind == EventKind::COMMIT)
        {
            last_commit = events[i].m_Detail;
        }
    }
    REQUIRE(last_commit.has_value());
    CHECK(last_commit->rfind("add_entry ", 0) == 0);
}

TEST_CASE("Draining at quit finishes the writes that are queued", "[viewmodel][threading][integration]")
{
    Rig rig(RigStore::SQLITE);
    import_and_open(rig);

    rig.repository().set_commit_delay(20ms);
    for (int i = 0; i < 5; ++i)
    {
        REQUIRE(rig.tap(type_id_at(rig, i)));
    }
    CHECK_FALSE(rig.idle());
    rig.root().drain();
    CHECK(rig.idle());
    CHECK(rig.session().pending_writes() == 0);
    CHECK(rig.timeline().entry_count() == 5);
    rig.repository().set_commit_delay(0ms);
    const auto stored = rig.stored_open_log();
    REQUIRE(stored.has_value());
    CHECK(stored->m_Log.entries().size() == 5);
}
