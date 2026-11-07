// SPDX-License-Identifier: GPL-3.0-or-later
#include "fake_time_zone_provider.hpp"
#include "support/test_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>
#include <vector>

using namespace std::chrono_literals;
using ej::viewmodel::NO_TIMER;
using ej::viewmodel::TimerId;

TEST_CASE("The test clock starts at 08:55 in Melbourne on 7 November 2026", "[viewmodel][support][clock]")
{
    const ej::test::TestClock clock;
    CHECK(clock.now() == ej::test::utc(2026, 11, 6, 21, 55));
}

TEST_CASE("Advancing moves the wall clock and the monotonic clock together", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    const auto wall = clock.now();
    const auto monotonic = clock.monotonic_now();

    clock.advance(2500ms);

    CHECK(clock.now() == wall + 2500ms);
    CHECK(clock.monotonic_now() == monotonic + 2500ms);
}

TEST_CASE("Stepping the wall clock does not move the monotonic clock or a timer", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    int fired = 0;
    static_cast<void>(clock.after(5s, [&] { ++fired; }));
    const auto monotonic = clock.monotonic_now();

    clock.step_wall_back(1h);
    clock.advance(4999ms);
    CHECK(fired == 0);
    clock.step_wall_forward(2h);
    clock.advance(1ms);

    CHECK(fired == 1);
    CHECK(clock.monotonic_now() == monotonic + 5s);
}

TEST_CASE("A timer fires once, when its delay has passed", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    int fired = 0;
    const TimerId timer = clock.after(5s, [&] { ++fired; });
    CHECK(static_cast<bool>(timer));
    CHECK(clock.pending_timers() == 1);

    clock.advance(4999ms);
    CHECK(fired == 0);
    clock.advance(1ms);
    CHECK(fired == 1);
    clock.advance(10s);
    CHECK(fired == 1);
    CHECK(clock.pending_timers() == 0);
}

TEST_CASE("Timers fire in order of their due time, ties in the order they were set", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    std::vector<std::string> order;
    static_cast<void>(clock.after(3s, [&] { order.emplace_back("c"); }));
    static_cast<void>(clock.after(1s, [&] { order.emplace_back("a"); }));
    static_cast<void>(clock.after(3s, [&] { order.emplace_back("d"); }));
    static_cast<void>(clock.after(2s, [&] { order.emplace_back("b"); }));

    clock.advance(10s);

    CHECK(order == std::vector<std::string>{"a", "b", "c", "d"});
}

TEST_CASE("Both clocks show the due time while a timer's callback runs", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    const auto wall = clock.now();
    const auto monotonic = clock.monotonic_now();
    ej::UtcMillis wall_seen;
    std::chrono::milliseconds monotonic_seen{};
    static_cast<void>(clock.after(2s,
                                  [&]
                                  {
                                      wall_seen = clock.now();
                                      monotonic_seen = clock.monotonic_now();
                                  }));

    clock.advance(10s);

    CHECK(wall_seen == wall + 2s);
    CHECK(monotonic_seen == monotonic + 2s);
    CHECK(clock.now() == wall + 10s);
}

TEST_CASE("A cancelled timer does not fire, and cancelling twice or cancelling nothing is harmless",
          "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    int fired = 0;
    const TimerId timer = clock.after(1s, [&] { ++fired; });

    clock.cancel(timer);
    clock.cancel(timer);
    clock.cancel(NO_TIMER);
    clock.advance(5s);

    CHECK(fired == 0);
    CHECK(clock.pending_timers() == 0);
}

TEST_CASE("A callback may set a timer that falls inside the same advance", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    std::vector<std::string> order;
    static_cast<void>(clock.after(1s,
                                  [&]
                                  {
                                      order.emplace_back("first");
                                      static_cast<void>(clock.after(1s, [&] { order.emplace_back("second"); }));
                                      static_cast<void>(clock.after(10s, [&] { order.emplace_back("never"); }));
                                  }));

    clock.advance(3s);

    CHECK(order == std::vector<std::string>{"first", "second"});
    CHECK(clock.pending_timers() == 1);
}

TEST_CASE("A callback may cancel a timer that is due later in the same advance", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    int later = 0;
    TimerId victim;
    static_cast<void>(clock.after(1s, [&] { clock.cancel(victim); }));
    victim = clock.after(2s, [&] { ++later; });

    clock.advance(5s);

    CHECK(later == 0);
}

TEST_CASE("A zero or negative delay is due now, and a negative advance does nothing", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    int fired = 0;
    static_cast<void>(clock.after(-5s, [&] { ++fired; }));
    const auto monotonic = clock.monotonic_now();

    clock.advance(-1s);
    CHECK(fired == 1);
    CHECK(clock.monotonic_now() == monotonic);
}

TEST_CASE("The wall clock can be put anywhere, 1970 included", "[viewmodel][support][clock]")
{
    ej::test::TestClock clock;
    clock.set_wall(ej::UtcMillis{});
    CHECK(clock.now() == ej::UtcMillis{});
    clock.advance(1s);
    CHECK(clock.now() == ej::UtcMillis{1s});
}
