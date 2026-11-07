// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/background_tasks.hpp"

#include <functional>
#include <optional>
#include <string_view>

namespace ej::bridge
{

enum class LifecycleEvent
{
    BECAME_INACTIVE, ///< Inactive, suspended or hidden: the application may go to the background.
    BECAME_ACTIVE,
    ABOUT_TO_QUIT,
};

/// What the coordinator does to the rest of the application, as functions so that it needs no Qt and a test needs no
/// model. All are called on the main thread.
struct LifecycleActions
{
    /// Writes the note draft now, skipping the debounce (`LogSession::flushDraft`, `DraftStore::flush`).
    std::function<void()> flush_draft;
    /// Waits for any write in flight and delivers its completion (`AppRoot::drain`).
    std::function<void()> drain;
    /// The day check (`DayWatcher::check`). Safe to call twice in a row.
    std::function<void()> check_day;
    /// Tells the haptic policy whether the application is active, so it prepares on becoming active.
    std::function<void(bool)> set_application_active;
    /// One line per event, by name (`lifecycle: inactive`), for the first device pass.
    std::function<void(std::string_view)> log;
};

/// What the application does when it goes inactive and comes back (NOTE-03, DAY-01, NFR-08).
///
/// - Going inactive: take a background task (so the flush finishes even if the phone is already going to the
/// background),
///   flush the draft, drain the writes, end the task. Nothing else is buffered: every acknowledged write is already
///   synced (Stage 2), so "flush the journal" means waiting for the one write that may be in flight. Doing it twice in
///   a row writes nothing the second time.
/// - Becoming active: tell the haptic policy, and run the day check. The check waits for `storage_ready()` on a cold
/// launch
///   (recovery still running) and runs on every activation after it. Active is also delivered at launch, so the check
///   is idempotent: two in a row raise one prompt.
/// - About to quit: the same flush, without a background task, best effort.
///
/// If the task expires before the flush has finished, the task is ended once and the expiry logged; recovery (Stage 2)
/// is the backstop, as it is for a kill. Main thread only.
class LifecycleCoordinator
{
public:
    LifecycleCoordinator(LifecycleActions actions, BackgroundTasks& tasks);

    void on_event(LifecycleEvent event);

    /// Recovery has finished and the log is loaded. A day check that was waiting for it runs now.
    void storage_ready();

private:
    void flush_in_task();
    void flush_now();
    void log(std::string_view line) const;
    void call(const std::function<void()>& action) const;

    LifecycleActions m_Actions;
    BackgroundTasks& m_Tasks;
    bool m_StorageReady = false;
    bool m_CheckWaiting = false;
    std::optional<BackgroundTasks::Token> m_OpenTask;
};

} // namespace ej::bridge
