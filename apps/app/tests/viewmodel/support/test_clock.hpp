// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "viewmodel/environment.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>

namespace ej::test
{

/// The wall clock and the monotonic scheduler of a test, driven by hand (docs/plan/stage-3.md, "Time, ids and the
/// clock"). Nothing sleeps.
///
/// `advance` moves both clocks by the same amount and fires the timers that fall due, in order of their due time (ties
/// in the order they were set), with both clocks showing the due time while the timer's callback runs. A callback may
/// set or cancel timers; one that falls inside the same `advance` fires in it. `step_wall_back` and `step_wall_forward`
/// move the wall clock only, as a person or the network changing the time does, and must change nothing a timer does.
class TestClock final : public Clock, public viewmodel::Scheduler
{
public:
    /// Sat 7 Nov 2026 08:55 in Melbourne (+11:00), the start of the full-day test.
    [[nodiscard]] static UtcMillis default_start();

    explicit TestClock(UtcMillis start = default_start());

    // Clock
    [[nodiscard]] UtcMillis now() const override
    {
        return m_Wall;
    }

    // Scheduler
    viewmodel::TimerId after(std::chrono::milliseconds delay, Callback callback) override;
    void cancel(viewmodel::TimerId timer) override;
    [[nodiscard]] std::chrono::milliseconds monotonic_now() const override
    {
        return m_Monotonic;
    }

    /// Moves the wall clock and the monotonic clock forward by `by` and fires the timers that fall due.
    void advance(std::chrono::milliseconds by);

    void step_wall_back(std::chrono::milliseconds by)
    {
        m_Wall -= by;
    }
    void step_wall_forward(std::chrono::milliseconds by)
    {
        m_Wall += by;
    }
    void set_wall(UtcMillis time)
    {
        m_Wall = time;
    }

    /// Timers set and neither fired nor cancelled.
    [[nodiscard]] std::size_t pending_timers() const
    {
        return m_Timers.size();
    }

private:
    struct Timer
    {
        std::chrono::milliseconds m_Due;
        Callback m_Callback;
    };

    UtcMillis m_Wall;
    std::chrono::milliseconds m_Monotonic{std::chrono::hours{1}}; // not zero, so nothing mistakes it for "unset"
    std::uint64_t m_LastTimer = 0;
    std::map<viewmodel::TimerId, Timer> m_Timers;
};

} // namespace ej::test
