// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app_root.hpp"
#include "error_journaling/clock.hpp"
#include "error_journaling/log_repository.hpp"
#include "error_journaling/recovery_report.hpp"
#include "error_journaling/stored_log.hpp"
#include "fake_time_zone_provider.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_platform.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/recording_repository.hpp"
#include "support/sequential_ids.hpp"
#include "support/test_clock.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/export_preferences.hpp"
#include "viewmodel/people_model.hpp"
#include "viewmodel/storage_gateway.hpp"

#include <QString>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace ej::test
{

/// Which store a `Rig` runs on (docs/plan/stage-3.md, "The exit-gate test").
enum class RigStore
{
    /// `unit`: the in-memory repository, a fake configuration store, `ManualStorageGateway`, the fixed-offset zone
    /// provider. Nothing runs until `settle()`, so a test looks at the state between "asked" and "committed".
    MEMORY,
    /// `integration`: `SqliteLogRepository` over the file journal and `FileConfigStore` in a temporary directory, a
    /// `ThreadedStorageGateway` with its storage thread, recovery at every start, and Qt's time zone provider.
    SQLITE
};

/// A clock the storage thread may read while the GUI thread moves it.
class SharedClock final : public Clock
{
public:
    explicit SharedClock(UtcMillis start) : m_Milliseconds(start.time_since_epoch().count()) {}

    [[nodiscard]] UtcMillis now() const override
    {
        return UtcMillis{std::chrono::milliseconds{m_Milliseconds.load()}};
    }
    void set(UtcMillis time)
    {
        m_Milliseconds.store(time.time_since_epoch().count());
    }

private:
    std::atomic<std::int64_t> m_Milliseconds;
};

/// The whole application, headless: every service built by `app::AppRoot` exactly as `main.cpp` builds it, on a fake
/// clock, ids, device zone, platform (haptics and share sheet) and clipboard. It is the fixture of the full-day test
/// (Stage 3, 3.11) and of the screen tests of Stage 4, which take `root()` and give its models to QML.
///
///     Rig rig(RigStore::MEMORY);
///     rig.import_sample_configuration();
///     rig.tap(type_id);              // the impact haptic, then LogSession::recordError
///     rig.settle();                  // the commit, then everything it sets off
///     CHECK(rig.board().totalCount() == 1);
///     rig.restart();                 // as a relaunch: every service and the gateway rebuilt on the same store
///
/// The `EventLog` holds, in order, every commit, haptic, toast change, share and clipboard copy. GUI thread only.
class Rig
{
public:
    explicit Rig(RigStore store = RigStore::MEMORY);
    ~Rig();

    Rig(const Rig&) = delete;
    Rig& operator=(const Rig&) = delete;

    // The services. They are the root's; after `restart()` they are new objects, so do not keep a reference across it.

    [[nodiscard]] app::AppRoot& root()
    {
        return *m_Root;
    }
    [[nodiscard]] viewmodel::LogSession& session()
    {
        return *m_Root->session();
    }
    [[nodiscard]] viewmodel::UndoController& undo()
    {
        return *m_Root->undo();
    }
    [[nodiscard]] viewmodel::BoardModel& board()
    {
        return *m_Root->board();
    }
    [[nodiscard]] viewmodel::ErrorPickerModel& picker()
    {
        return *m_Root->picker();
    }
    [[nodiscard]] viewmodel::TimelineModel& timeline()
    {
        return *m_Root->timeline();
    }
    [[nodiscard]] viewmodel::HistoryModel& history()
    {
        return *m_Root->history();
    }
    [[nodiscard]] viewmodel::DayWatcher& day()
    {
        return *m_Root->day();
    }
    [[nodiscard]] viewmodel::SettingsModel& settings()
    {
        return *m_Root->settings();
    }
    [[nodiscard]] viewmodel::ShareService& share()
    {
        return *m_Root->share();
    }

    // The world.

    [[nodiscard]] RigStore store_kind() const
    {
        return m_Kind;
    }
    [[nodiscard]] TestClock& clock()
    {
        return m_Clock;
    }
    [[nodiscard]] EventLog& events()
    {
        return m_Events;
    }
    /// The share sheet and the haptic engine.
    [[nodiscard]] FakePlatform& platform()
    {
        return m_Platform;
    }
    [[nodiscard]] FakeClipboard& clipboard()
    {
        return m_Clipboard;
    }
    /// The preferences of how text that leaves the device is written (an ini file nowhere: kept in memory).
    [[nodiscard]] viewmodel::ExportPreferences& export_preferences()
    {
        return m_ExportPreferences;
    }
    [[nodiscard]] SequentialIds& ids()
    {
        return m_Ids;
    }
    /// The repository wrapper of the current store, which records every call and the thread it was made on.
    [[nodiscard]] RecordingRepository& repository();
    /// The in-memory store (`MEMORY` only).
    [[nodiscard]] MemoryStorage& memory();
    /// The gateway the root runs on.
    [[nodiscard]] viewmodel::StorageGateway& gateway()
    {
        return m_Root->gateway();
    }

    // Time. The clock starts at Sat 7 Nov 2026 08:55 in Melbourne (+11:00).

    /// Moves the clock to a local time in Melbourne on a day of November 2026. Later times are reached with `advance`,
    /// so the timers on the way (the undo window, the draft debounce) fire; an earlier time only sets the wall clock.
    void go_to(int day, int hour, int minute, int second = 0, int millisecond = 0);

    /// Moves both clocks forward and fires the timers that fall due.
    void advance(std::chrono::milliseconds by);

    /// The local time of day of the fake clock in Melbourne, as "HH:MM:SS".
    [[nodiscard]] std::string local_time_text() const;

    // Driving.

    /// Runs the store until it is idle and every completion has been delivered. `MEMORY`: runs the manual gateway.
    /// `SQLITE`: spins the event loop until the gateway has nothing outstanding. A held commit never finishes, so a
    /// test that holds one releases it first.
    void settle();

    /// Spins the event loop for a moment (`SQLITE`) so that anything that has arrived is delivered; `MEMORY`: nothing.
    /// For a test that looks at the state while a commit is held back.
    void pump();

    /// Calls `done` repeatedly, settling the store in between, until it returns true or ten seconds have passed (the
    /// test fails then).
    void wait_until(const std::function<bool()>& done);

    /// The press of a tile: the touch-down impact haptic fires first, as QML does, and then `recordError` is called.
    /// Returns what `recordError` returned.
    bool tap(const QString& type_id);

    /// The Add button of the composer: `addNote`. No haptic at the press (the success haptic follows the commit).
    bool add_note(const QString& text);

    /// Holds every write back at the store. `MEMORY`: jobs wait for `settle()` anyway, so this only marks the intent.
    /// `SQLITE`: the repository wrapper blocks every commit on a latch until `release_commits()`.
    void hold_commits();
    /// Lets the held writes through and settles.
    void release_commits();

    /// `SQLITE`: the next `restart()` queues a job that keeps the storage thread busy before the services load, so the
    /// first frame is seen as it is while the store has not answered: `LOADING`, with empty models. `restart()` then
    /// returns without settling. `release_startup()` lets the store through and settles.
    void hold_startup();
    void release_startup();

    /// Reads the sample configuration through `SettingsModel` as the import screen does (`loadImport`, then
    /// `applyImport`), and settles. The model is `configured` afterwards.
    void import_sample_configuration();

    /// The next `restart()` hands this report to the session as the outcome of start-up recovery, once, so that the Log
    /// screen shows the recovery notice (DS-04) without a damaged store; the visual capture uses it.
    void set_startup_recovery(RecoveryReport report)
    {
        m_PendingRecovery = std::move(report);
    }

    /// Destroys every service and the gateway and builds them again on the same store, as a relaunch does. `SQLITE`:
    /// the store is closed and `Recovery::run` runs before it is opened again. Settles first.
    void restart();

    // The store, read through the gateway (so a read happens on the storage thread, like every other).

    template <typename Work>
    [[nodiscard]] auto in_store(Work work)
    {
        using Product = std::invoke_result_t<Work&, viewmodel::StorageContext&>;
        std::optional<Product> product;
        gateway().run(std::move(work), [&product](Product result) { product.emplace(std::move(result)); });
        wait_until([&product] { return product.has_value(); });
        return std::move(*product);
    }

    [[nodiscard]] std::vector<LogSummary> stored_summaries();
    [[nodiscard]] StoredLog stored_log(LogId id);
    [[nodiscard]] std::optional<StoredLog> stored_open_log();
    [[nodiscard]] std::optional<Draft> stored_draft();

    /// Every repository call that changed the store, over all the stores this rig has opened.
    [[nodiscard]] std::size_t mutating_calls();
    /// The threads that made repository calls, over all the stores this rig has opened.
    [[nodiscard]] std::set<std::thread::id> repository_threads();

    /// True when the store has nothing queued, running or waiting to be delivered.
    [[nodiscard]] bool idle() const;

    /// How many times `AppRoot::writeFailed` was emitted since the rig was made.
    [[nodiscard]] int write_failures() const
    {
        return m_WriteFailures;
    }

private:
    void build();
    void tear_down();
    void watch_events();
    void harvest_records();
    [[nodiscard]] viewmodel::Environment environment();

    RigStore m_Kind;
    EventLog m_Events;
    TestClock m_Clock;
    SharedClock m_StoreClock;
    SequentialIds m_Ids;
    FakeDeviceZone m_DeviceZone;
    FakeTimeZoneProvider m_FakeZones;
    std::unique_ptr<TimeZoneProvider> m_QtZones;
    FakePlatform m_Platform;
    FakeClipboard m_Clipboard;
    viewmodel::ExportPreferences m_ExportPreferences{QString()};
    viewmodel::PersonNamesForwarder m_PersonNames; // points at the root's list of people; cleared before it goes

    // MEMORY: the store outlives a restart.
    std::unique_ptr<MemoryStorage> m_Memory;
    ManualStorageGateway* m_Manual = nullptr;

    // SQLITE
    QTemporaryDir m_Directory;
    std::atomic<RecordingRepository*> m_Recording{nullptr};
    viewmodel::ThreadedStorageGateway* m_Threaded = nullptr;
    std::mutex m_HoldMutex;
    std::condition_variable m_HoldReleased;
    bool m_Holding = false;
    bool m_HoldNextStart = false;
    bool m_StartGateClosed = false;
    std::size_t m_PastMutatingCalls = 0;
    std::set<std::thread::id> m_PastThreads;

    std::optional<RecoveryReport> m_PendingRecovery;

    bool m_Watching = false;
    int m_WriteFailures = 0;
    std::unique_ptr<app::AppRoot> m_Root;
};

} // namespace ej::test
