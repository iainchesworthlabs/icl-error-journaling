// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/timestamp.hpp"

#include <chrono>
#include <deque>
#include <utility>

namespace ej
{

/// The wall clock, injected so that tests control time. Recovery and replay never read it (section 3.4 of the Stage 2
/// plan): ordering in storage is by journal sequence number, never by wall time.
class Clock
{
public:
    Clock() = default;
    Clock(const Clock&) = delete;
    Clock& operator=(const Clock&) = delete;
    virtual ~Clock() = default;

    [[nodiscard]] virtual UtcMillis now() const = 0;
};

/// The system clock. The only implementation the product uses.
class SystemClock final : public Clock
{
public:
    [[nodiscard]] UtcMillis now() const override;
};

/// A clock a test drives by hand: it answers the queued times in order and then keeps answering the last one, so a test
/// can step the clock backwards, hold it still, or put it at 1970.
class ScriptedClock final : public Clock
{
public:
    explicit ScriptedClock(UtcMillis start = UtcMillis{}) : m_Current(start) {}

    [[nodiscard]] UtcMillis now() const override
    {
        if (!m_Queue.empty())
        {
            m_Current = m_Queue.front();
            m_Queue.pop_front();
        }
        return m_Current;
    }

    /// The next calls to `now` answer this time and stay there.
    void set(UtcMillis time)
    {
        m_Queue.clear();
        m_Current = time;
    }

    void advance(std::chrono::milliseconds by)
    {
        set(m_Current + by);
    }

    /// The next call to `now` answers `time`, the one after the next queued time, and so on.
    void queue(UtcMillis time)
    {
        m_Queue.push_back(time);
    }

private:
    mutable UtcMillis m_Current;
    mutable std::deque<UtcMillis> m_Queue;
};

} // namespace ej
