// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include <clocale>
#include <cstdlib>
#include <string>

#include "error_journaling/time_format.hpp"
#include "fake_time_zone_provider.hpp"

using namespace ej;
using ej::test::local;

namespace
{
constexpr TimeFormat H24{TimeStyle::H24, false, MeridiemCase::UPPER};
constexpr TimeFormat H24S{TimeStyle::H24, true, MeridiemCase::UPPER};
constexpr TimeFormat H12U{TimeStyle::H12, false, MeridiemCase::UPPER};
constexpr TimeFormat H12L{TimeStyle::H12, false, MeridiemCase::LOWER};
constexpr TimeFormat H12US{TimeStyle::H12, true, MeridiemCase::UPPER};
constexpr TimeFormat H12LS{TimeStyle::H12, true, MeridiemCase::LOWER};
}  // namespace

TEST_CASE("24-hour time", "[time_format]")
{
    CHECK(format_time(local(2026, 11, 7, 0, 0), H24) == "00:00");
    CHECK(format_time(local(2026, 11, 7, 0, 5), H24) == "00:05");
    CHECK(format_time(local(2026, 11, 7, 9, 7), H24) == "09:07");
    CHECK(format_time(local(2026, 11, 7, 12, 0), H24) == "12:00");
    CHECK(format_time(local(2026, 11, 7, 23, 59), H24) == "23:59");
    CHECK(format_time(local(2026, 11, 7, 10, 5), TimeFormat{}) == "10:05");
}

TEST_CASE("12-hour time, upper case", "[time_format]")
{
    CHECK(format_time(local(2026, 11, 7, 0, 0), H12U) == "12:00 AM");
    CHECK(format_time(local(2026, 11, 7, 0, 5), H12U) == "12:05 AM");
    CHECK(format_time(local(2026, 11, 7, 9, 7), H12U) == "09:07 AM");
    CHECK(format_time(local(2026, 11, 7, 12, 0), H12U) == "12:00 PM");
    CHECK(format_time(local(2026, 11, 7, 13, 15), H12U) == "01:15 PM");
    CHECK(format_time(local(2026, 11, 7, 23, 59), H12U) == "11:59 PM");
}

TEST_CASE("12-hour time, lower case", "[time_format]")
{
    CHECK(format_time(local(2026, 11, 7, 0, 0), H12L) == "12:00 am");
    CHECK(format_time(local(2026, 11, 7, 0, 5), H12L) == "12:05 am");
    CHECK(format_time(local(2026, 11, 7, 9, 7), H12L) == "09:07 am");
    CHECK(format_time(local(2026, 11, 7, 12, 0), H12L) == "12:00 pm");
    CHECK(format_time(local(2026, 11, 7, 13, 15), H12L) == "01:15 pm");
    CHECK(format_time(local(2026, 11, 7, 23, 59), H12L) == "11:59 pm");
}

TEST_CASE("Seconds", "[time_format]")
{
    const LocalTime t = local(2026, 11, 7, 9, 7, 3);
    CHECK(format_time(t, H24S) == "09:07:03");
    CHECK(format_time(t, H12US) == "09:07:03 AM");
    CHECK(format_time(t, H12LS) == "09:07:03 am");
    CHECK(format_time(local(2026, 11, 7, 23, 59, 59), H12LS) == "11:59:59 pm");
    CHECK(format_time(local(2026, 11, 7, 0, 0, 0), H24S) == "00:00:00");
}

TEST_CASE("All eight combinations of the format", "[time_format]")
{
    const LocalTime t = local(2026, 11, 7, 15, 4, 9);
    for (const bool seconds : {false, true})
    {
        for (const MeridiemCase meridiem : {MeridiemCase::UPPER, MeridiemCase::LOWER})
        {
            const bool upper = meridiem == MeridiemCase::UPPER;
            CHECK(format_time(t, {TimeStyle::H24, seconds, meridiem}) == (seconds ? "15:04:09" : "15:04"));
            const std::string expected = std::string(seconds ? "03:04:09" : "03:04") + (upper ? " PM" : " pm");
            CHECK(format_time(t, {TimeStyle::H12, seconds, meridiem}) == expected);
        }
    }
}

TEST_CASE("Output does not depend on the locale", "[time_format]")
{
    const char* const before = std::setlocale(LC_ALL, nullptr);
    const std::string saved = before != nullptr ? before : "C";
    // Not every machine has these locales; the check still holds when the call falls back to "C".
    for (const char* name : {"de_DE.UTF-8", "fr_FR.UTF-8", "ar_SA.UTF-8", "German_Germany.1252"})
    {
        std::setlocale(LC_ALL, name);
        CHECK(format_time(local(2026, 11, 7, 9, 7, 3), H12US) == "09:07:03 AM");
        CHECK(format_time(local(2026, 11, 7, 21, 7), H24) == "21:07");
    }
    std::setlocale(LC_ALL, saved.c_str());
}
