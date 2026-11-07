// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace ej;
using namespace std::chrono_literals;

TEST_CASE("The system clock reports a time near the real one", "[clock]")
{
    const SystemClock clock;
    const auto before = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    const UtcMillis now = clock.now();
    const auto after = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
    CHECK(now >= before);
    CHECK(now <= after);
}

TEST_CASE("A scripted clock holds still, advances, steps back and answers a queue in order", "[clock]")
{
    const UtcMillis start{1'000s};
    ScriptedClock clock(start);
    CHECK(clock.now() == start);
    CHECK(clock.now() == start);

    clock.advance(500ms);
    CHECK(clock.now() == start + 500ms);

    clock.set(UtcMillis{}); // the device clock was reset to 1970
    CHECK(clock.now() == UtcMillis{});

    clock.set(start);
    clock.queue(start + 10s);
    clock.queue(start - 10s); // steps backwards
    CHECK(clock.now() == start + 10s);
    CHECK(clock.now() == start - 10s);
    CHECK(clock.now() == start - 10s); // the last answer stays
}
