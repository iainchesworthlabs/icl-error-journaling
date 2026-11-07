// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/iso_date.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace std::chrono;

TEST_CASE("A date is written as four digit year, month and day", "[iso-date]")
{
    CHECK(format_iso_date(2026y / 11 / 7) == "2026-11-07");
    CHECK(format_iso_date(1970y / 1 / 1) == "1970-01-01");
    CHECK(format_iso_date(0y / 1 / 1) == "0000-01-01");
    CHECK(format_iso_date(9999y / 12 / 31) == "9999-12-31");
}

TEST_CASE("A date that does not exist or has no four digit year is not written", "[iso-date]")
{
    CHECK_FALSE(format_iso_date(2026y / 2 / 30).has_value());
    CHECK_FALSE(format_iso_date(10000y / 1 / 1).has_value());
    CHECK_FALSE(format_iso_date(year{-1} / 1 / 1).has_value());
}

TEST_CASE("A written date reads back, and nothing else does", "[iso-date]")
{
    CHECK(parse_iso_date("2026-11-07") == 2026y / 11 / 7);
    CHECK(parse_iso_date("2024-02-29") == 2024y / 2 / 29);
    for (const char* bad : {"", "2026-11-7", "2026/11/07", "2026-13-01", "2026-02-30", "2025-02-29", "abcd-ef-gh",
                            "2026-11-07 ", "+026-11-07", "2026-1a-07", "-026-11-07"})
    {
        INFO(bad);
        CHECK_FALSE(parse_iso_date(bad).has_value());
    }
}
