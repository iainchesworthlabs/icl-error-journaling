// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "error_journaling/uuid.hpp"
#include "viewmodel/move_only_function.hpp"

#include <chrono>
#include <compare>
#include <cstdint>
#include <string>

namespace ej::viewmodel
{

/// Names one pending timer of a `Scheduler`. The default value names none.
struct TimerId
{
    std::uint64_t m_Value = 0;

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return m_Value != 0;
    }
    friend constexpr auto operator<=>(const TimerId&, const TimerId&) = default;
};

inline constexpr TimerId NO_TIMER{};

/// Monotonic one-shot timers, on the GUI thread. The undo window is the only user in Stage 3. A change of the wall
/// clock must neither extend nor cut a timer, which is why this is not built on `Clock`.
class Scheduler
{
public:
    using Callback = MoveOnlyFunction<void()>;

    Scheduler() = default;
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;
    virtual ~Scheduler() = default;

    /// Calls `callback` once, no earlier than `delay` from now on the monotonic clock.
    virtual TimerId after(std::chrono::milliseconds delay, Callback callback) = 0;

    /// Cancels a timer that has not fired. A timer that has fired, was cancelled or is `NO_TIMER` is ignored.
    virtual void cancel(TimerId timer) = 0;

    [[nodiscard]] virtual std::chrono::milliseconds monotonic_now() const = 0;
};

/// Where new entry and log ids come from. Production: two words of `QRandomGenerator::system()` through
/// `Uuid::from_random`. The caller of a command takes the id when it accepts the command, which is what makes the
/// replay of a journal idempotent (Stage 2, decision 12).
class IdGenerator
{
public:
    IdGenerator() = default;
    IdGenerator(const IdGenerator&) = delete;
    IdGenerator& operator=(const IdGenerator&) = delete;
    virtual ~IdGenerator() = default;

    [[nodiscard]] virtual Uuid next() = 0;

    /// `ids.next_as<EntryId>()`.
    template <typename Id>
    [[nodiscard]] Id next_as()
    {
        return Id(next());
    }
};

/// The zone the device is in now, as an IANA name. Production: `QTimeZone::systemTimeZoneId()`.
class DeviceZone
{
public:
    DeviceZone() = default;
    DeviceZone(const DeviceZone&) = delete;
    DeviceZone& operator=(const DeviceZone&) = delete;
    virtual ~DeviceZone() = default;

    [[nodiscard]] virtual std::string current() const = 0;
};

/// Everything a service needs to know about the world outside the program, as references. The services take one of
/// these and nothing else, so a test replaces time, ids and the zone by changing what the struct refers to.
struct Environment
{
    const Clock& m_Clock;
    Scheduler& m_Scheduler;
    IdGenerator& m_Ids;
    const DeviceZone& m_Zone;
    const TimeZoneProvider& m_Zones;
};

} // namespace ej::viewmodel
