// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "fake_time_zone_provider.hpp"
#include "support/sequential_ids.hpp"
#include "support/test_clock.hpp"
#include "viewmodel/environment.hpp"

#include <chrono>

namespace ej::test
{

/// The sample configuration (`config/sample-config.json`: Sam Example, UNIT, Wrong Response, North Site and the other
/// fictional stand-ins), read and validated. Fails the test that calls it if the file does not import.
[[nodiscard]] Configuration sample_configuration();

/// The zone provider of the full-day test: fixed offsets, Australia/Melbourne at +11:00 (right for 7 and 8 November
/// 2026), Australia/Perth at +08:00 and UTC.
[[nodiscard]] FakeTimeZoneProvider make_fixed_zones();

/// The time, the ids, the device zone and the zones of a test, and the `Environment` that refers to them.
///
///     TestEnvironment world;
///     LogSession session(world.environment(), ...);
///     world.clock.advance(std::chrono::seconds(2));
struct TestEnvironment
{
    TestClock m_Clock;
    SequentialIds m_Ids;
    FakeDeviceZone m_DeviceZone;
    FakeTimeZoneProvider m_Zones = make_fixed_zones();

    [[nodiscard]] viewmodel::Environment environment()
    {
        return viewmodel::Environment{m_Clock, m_Clock, m_Ids, m_DeviceZone, m_Zones};
    }
};

} // namespace ej::test
