// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/test_clock.hpp"

#include <algorithm>
#include <utility>

namespace ej::test
{

UtcMillis TestClock::default_start()
{
    using namespace std::chrono;
    // 08:55 at +11:00 is 21:55 the day before in UTC.
    return UtcMillis{duration_cast<milliseconds>(sys_days{year{2026} / 11 / 6}.time_since_epoch()) + hours{21} +
                     minutes{55}};
}

TestClock::TestClock(UtcMillis start) : m_Wall(start) {}

viewmodel::TimerId TestClock::after(std::chrono::milliseconds delay, Callback callback)
{
    const viewmodel::TimerId id{++m_LastTimer};
    m_Timers.emplace(id, Timer{m_Monotonic + std::max(delay, std::chrono::milliseconds::zero()), std::move(callback)});
    return id;
}

void TestClock::cancel(viewmodel::TimerId timer)
{
    m_Timers.erase(timer);
}

void TestClock::advance(std::chrono::milliseconds by)
{
    const auto target = m_Monotonic + std::max(by, std::chrono::milliseconds::zero());
    while (true)
    {
        // The earliest due time; the map is ordered by id, so a tie goes to the timer that was set first.
        auto next = m_Timers.end();
        for (auto it = m_Timers.begin(); it != m_Timers.end(); ++it)
        {
            if (it->second.m_Due <= target && (next == m_Timers.end() || it->second.m_Due < next->second.m_Due))
            {
                next = it;
            }
        }
        if (next == m_Timers.end())
        {
            break;
        }
        const auto due = std::max(next->second.m_Due, m_Monotonic);
        m_Wall += due - m_Monotonic;
        m_Monotonic = due;
        Callback callback = std::move(next->second.m_Callback);
        m_Timers.erase(next);
        callback();
    }
    m_Wall += target - m_Monotonic;
    m_Monotonic = target;
}

} // namespace ej::test
