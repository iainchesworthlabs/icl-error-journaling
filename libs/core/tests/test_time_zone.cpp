// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <chrono>

#include "error_journaling/time_zone_provider.hpp"
#include "fake_time_zone_provider.hpp"

using namespace ej;
using namespace std::chrono_literals;
using ej::test::local;
using ej::test::utc;

namespace
{
constexpr auto MELBOURNE = "Australia/Melbourne";

LocalTime at(const TimeZoneProvider& zones, UtcMillis instant, const char* zone)
{
    const auto result = zones.to_local(Timestamp{instant, zone});
    REQUIRE(result.has_value());
    return *result;
}

bool same(const LocalTime& a, const LocalTime& b)
{
    return a.m_Date == b.m_Date && a.m_SinceMidnight == b.m_SinceMidnight;
}
}  // namespace

TEST_CASE("Qt provider: Melbourne changes offset on 2026-10-04", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    // 2026-10-03 12:00 AEST (+10:00) is 02:00 UTC; 2026-10-05 12:00 AEDT (+11:00) is 01:00 UTC.
    CHECK(same(at(*zones, utc(2026, 10, 3, 2, 0), MELBOURNE), local(2026, 10, 3, 12, 0)));
    CHECK(same(at(*zones, utc(2026, 10, 5, 1, 0), MELBOURNE), local(2026, 10, 5, 12, 0)));
    // The last instant before and the first after the change: 15:59:59 UTC is 01:59:59 AEST, 16:00 UTC is 03:00 AEDT.
    CHECK(same(at(*zones, utc(2026, 10, 3, 15, 59, 59), MELBOURNE), local(2026, 10, 4, 1, 59, 59)));
    CHECK(same(at(*zones, utc(2026, 10, 3, 16, 0), MELBOURNE), local(2026, 10, 4, 3, 0)));
}

TEST_CASE("Qt provider: a local time in the spring gap resolves to a defined instant", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const auto instant = zones->to_utc(local(2026, 10, 4, 2, 30), MELBOURNE);
    REQUIRE(instant.has_value());
    // The instant lies on or after the end of the gap (16:00 UTC) and the local date stays 4 October.
    CHECK(*instant >= utc(2026, 10, 3, 16, 0));
    const LocalTime back = at(*zones, *instant, MELBOURNE);
    CHECK(back.m_Date == std::chrono::year{2026} / 10 / 4);
    CHECK(back.m_SinceMidnight >= 3h);
}

TEST_CASE("Qt provider: the repeated hour in April", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    // 02:30 happens at 15:30 UTC (AEDT, +11:00) on 4 April and again at 16:30 UTC (AEST, +10:00).
    const UtcMillis first = utc(2026, 4, 4, 15, 30);
    const UtcMillis second = utc(2026, 4, 4, 16, 30);
    CHECK(same(at(*zones, first, MELBOURNE), local(2026, 4, 5, 2, 30)));
    CHECK(same(at(*zones, second, MELBOURNE), local(2026, 4, 5, 2, 30)));

    const auto picked = zones->to_utc(local(2026, 4, 5, 2, 30), MELBOURNE);
    REQUIRE(picked.has_value());
    CHECK(*picked == first);
}

TEST_CASE("Qt provider: to_utc and to_local round-trip outside a change", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const auto instant = zones->to_utc(local(2026, 11, 7, 10, 5, 9), MELBOURNE);
    REQUIRE(instant.has_value());
    CHECK(*instant == utc(2026, 11, 6, 23, 5, 9));
}

TEST_CASE("Qt provider: Perth, Sydney and UTC for the traveller", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const UtcMillis instant = utc(2026, 11, 7, 2, 0);
    CHECK(same(at(*zones, instant, "Australia/Perth"), local(2026, 11, 7, 10, 0)));
    CHECK(same(at(*zones, instant, "Australia/Sydney"), local(2026, 11, 7, 13, 0)));
    CHECK(same(at(*zones, instant, "Australia/Melbourne"), local(2026, 11, 7, 13, 0)));
    CHECK(same(at(*zones, instant, "UTC"), local(2026, 11, 7, 2, 0)));
}

TEST_CASE("Qt provider: the local date can differ from the UTC date", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    // UTC 13:30 on 6 November is 00:30 on 7 November in Melbourne (+11:00).
    CHECK(same(at(*zones, utc(2026, 11, 6, 13, 30), MELBOURNE), local(2026, 11, 7, 0, 30)));
}

TEST_CASE("Qt provider: milliseconds are truncated, not rounded", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    CHECK(same(at(*zones, utc(2026, 11, 7, 2, 0, 59, 999), "UTC"), local(2026, 11, 7, 2, 0, 59)));
}

TEST_CASE("Qt provider: an unknown zone is an error", "[time_zone]")
{
    const auto zones = make_qt_time_zone_provider();
    const auto a = zones->to_local(Timestamp{utc(2026, 11, 7, 2, 0), "Nowhere/North_Site"});
    REQUIRE_FALSE(a.has_value());
    CHECK(a.error() == TimeZoneProvider::Error::UNKNOWN_ZONE);
    const auto b = zones->to_utc(local(2026, 11, 7, 2, 0), "Nowhere/North_Site");
    REQUIRE_FALSE(b.has_value());
    CHECK(b.error() == TimeZoneProvider::Error::UNKNOWN_ZONE);
    CHECK_FALSE(zones->to_local(Timestamp{utc(2026, 11, 7, 2, 0), ""}).has_value());
}

TEST_CASE("Fake provider: fixed offsets", "[time_zone]")
{
    const test::FakeTimeZoneProvider fake{{{"Zone/A", 10h}, {"Zone/B", -5h - 30min}}};
    CHECK(same(at(fake, utc(2026, 11, 6, 23, 5, 9), "Zone/A"), local(2026, 11, 7, 9, 5, 9)));
    CHECK(same(at(fake, utc(2026, 11, 7, 2, 0), "Zone/B"), local(2026, 11, 6, 20, 30)));
    CHECK(*fake.to_utc(local(2026, 11, 7, 9, 5, 9), "Zone/A") == utc(2026, 11, 6, 23, 5, 9));
    CHECK(fake.to_local(Timestamp{utc(2026, 11, 7, 2, 0), "Zone/C"}).error() == TimeZoneProvider::Error::UNKNOWN_ZONE);
}
