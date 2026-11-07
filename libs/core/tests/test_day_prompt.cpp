// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "error_journaling/day_prompt.hpp"
#include "fake_time_zone_provider.hpp"

using namespace ej;
using namespace std::chrono_literals;
using ej::test::utc;

namespace
{
constexpr auto MELBOURNE = "Australia/Melbourne";
constexpr auto PERTH = "Australia/Perth";

std::chrono::year_month_day ymd(int y, unsigned m, unsigned d)
{
    return std::chrono::year{y} / std::chrono::month{m} / std::chrono::day{d};
}

DayPrompt prompt(const TimeZoneProvider& zones, std::chrono::year_month_day log, UtcMillis now, const char* zone)
{
    const auto result = day_prompt(log, Timestamp{now, zone}, zones);
    REQUIRE(result.has_value());
    return *result;
}
}  // namespace

TEST_CASE("day_prompt with fixed offsets", "[day_prompt]")
{
    const test::FakeTimeZoneProvider zones{{{"Zone/A", 10h}}};
    const auto log = ymd(2026, 11, 7);
    // Zone/A is UTC+10, so local midnight is 14:00 UTC.
    CHECK(prompt(zones, log, utc(2026, 11, 7, 3, 0), "Zone/A") == DayPrompt::NONE);
    CHECK(prompt(zones, log, utc(2026, 11, 7, 13, 59, 59, 999), "Zone/A") == DayPrompt::NONE);
    CHECK(prompt(zones, log, utc(2026, 11, 7, 14, 0), "Zone/A") == DayPrompt::OFFER_NEW_LOG);
    CHECK(prompt(zones, log, utc(2026, 11, 10, 3, 0), "Zone/A") == DayPrompt::OFFER_NEW_LOG);
    CHECK(prompt(zones, log, utc(2026, 11, 5, 3, 0), "Zone/A") == DayPrompt::NONE);
}

TEST_CASE("A log that runs past midnight stays open until the prompt is accepted", "[day_prompt]")
{
    const test::FakeTimeZoneProvider zones{{{"Zone/A", 10h}}};
    const auto log = ymd(2026, 11, 7);
    // Entries at 23:50 and 00:10 local both belong to the log; the prompt is raised at 00:10.
    CHECK(prompt(zones, log, utc(2026, 11, 7, 13, 50), "Zone/A") == DayPrompt::NONE);
    CHECK(prompt(zones, log, utc(2026, 11, 7, 14, 10), "Zone/A") == DayPrompt::OFFER_NEW_LOG);
    // Declined, and asked again on the next foreground.
    CHECK(prompt(zones, log, utc(2026, 11, 7, 14, 40), "Zone/A") == DayPrompt::OFFER_NEW_LOG);
}

TEST_CASE("local_date follows the timestamp's own zone", "[day_prompt]")
{
    const test::FakeTimeZoneProvider zones{{{"Zone/A", 10h}, {"Zone/B", -5h}}};
    const UtcMillis instant = utc(2026, 11, 7, 3, 0);
    CHECK(*local_date(Timestamp{instant, "Zone/A"}, zones) == ymd(2026, 11, 7));
    CHECK(*local_date(Timestamp{instant, "Zone/B"}, zones) == ymd(2026, 11, 6));
    CHECK(local_date(Timestamp{instant, "Zone/C"}, zones).error() == TimeZoneProvider::Error::UNKNOWN_ZONE);
    CHECK(day_prompt(ymd(2026, 11, 7), Timestamp{instant, "Zone/C"}, zones).error() ==
          TimeZoneProvider::Error::UNKNOWN_ZONE);
}

TEST_CASE("Melbourne's 23-hour day rolls over at local midnight", "[day_prompt][time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const auto log = ymd(2026, 10, 4);
    // 4 October has 23 hours: local midnight 5 October is 13:00 UTC on the 4th (AEDT, +11:00).
    CHECK(prompt(*zones, log, utc(2026, 10, 4, 12, 59, 59, 999), MELBOURNE) == DayPrompt::NONE);
    CHECK(prompt(*zones, log, utc(2026, 10, 4, 13, 0), MELBOURNE) == DayPrompt::OFFER_NEW_LOG);
    // A log opened the day before is already due at the start of the 4th (14:00 UTC on the 3rd, +10:00).
    CHECK(prompt(*zones, ymd(2026, 10, 3), utc(2026, 10, 3, 13, 59), MELBOURNE) == DayPrompt::NONE);
    CHECK(prompt(*zones, ymd(2026, 10, 3), utc(2026, 10, 3, 14, 0), MELBOURNE) == DayPrompt::OFFER_NEW_LOG);
}

TEST_CASE("Melbourne's 25-hour day rolls over at local midnight", "[day_prompt][time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const auto log = ymd(2026, 4, 5);
    // 5 April has 25 hours: it starts at 13:00 UTC on the 4th (AEDT) and ends at 14:00 UTC on the 5th (AEST).
    CHECK(prompt(*zones, log, utc(2026, 4, 5, 13, 59, 59, 999), MELBOURNE) == DayPrompt::NONE);
    CHECK(prompt(*zones, log, utc(2026, 4, 5, 14, 0), MELBOURNE) == DayPrompt::OFFER_NEW_LOG);
    // 24 hours after the day began is not yet midnight.
    CHECK(prompt(*zones, log, utc(2026, 4, 5, 13, 0), MELBOURNE) == DayPrompt::NONE);
}

TEST_CASE("A traveller who flies west keeps the log until local midnight", "[day_prompt][time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    // The log was opened in Melbourne on 7 November. Perth is two hours behind (no daylight saving).
    const auto log = ymd(2026, 11, 7);
    // 22:30 Perth on the 7th is 14:30 UTC, already 01:30 on the 8th in Melbourne.
    CHECK(prompt(*zones, log, utc(2026, 11, 7, 14, 30), PERTH) == DayPrompt::NONE);
    CHECK(prompt(*zones, log, utc(2026, 11, 7, 14, 30), MELBOURNE) == DayPrompt::OFFER_NEW_LOG);
    // 00:30 Perth on the 8th is 16:30 UTC.
    CHECK(prompt(*zones, log, utc(2026, 11, 7, 16, 30), PERTH) == DayPrompt::OFFER_NEW_LOG);
}

TEST_CASE("A traveller who flies east gets the prompt earlier", "[day_prompt][time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    // A log opened in Perth on 7 November; at 23:00 Perth (15:00 UTC) it is 02:00 on the 8th in Melbourne.
    const auto log = ymd(2026, 11, 7);
    CHECK(prompt(*zones, log, utc(2026, 11, 7, 15, 0), PERTH) == DayPrompt::NONE);
    CHECK(prompt(*zones, log, utc(2026, 11, 7, 15, 0), MELBOURNE) == DayPrompt::OFFER_NEW_LOG);
}

TEST_CASE("day_prompt with the Qt provider and an unknown zone", "[day_prompt][time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const auto result = day_prompt(ymd(2026, 11, 7), Timestamp{utc(2026, 11, 7, 1, 0), "Nowhere/North_Site"}, *zones);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == TimeZoneProvider::Error::UNKNOWN_ZONE);
}
