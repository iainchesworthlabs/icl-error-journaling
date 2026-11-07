// SPDX-License-Identifier: GPL-3.0-or-later
//
// The core's test helpers, seen from the view-model tests: the golden comparison, the scenarios behind the golden files
// and the fixed-offset zone provider. A test of a service renders with them and compares with the same files the core
// tests do, so the PRD example stays the one contract.
#include "error_journaling/export_renderer.hpp"
#include "fake_time_zone_provider.hpp"
#include "golden.hpp"
#include "golden_scenarios.hpp"
#include "support/test_environment.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ej;

TEST_CASE("The PRD example renders to the golden file from the view-model tests", "[viewmodel][support][golden]")
{
    const auto scenario = test::make_scenario("prd-example");

    const auto text = render_export(scenario.m_Log, scenario.m_Context);

    REQUIRE(text.has_value());
    test::check_golden("prd-example", *text);
}

TEST_CASE("A golden mismatch is reported with the first differing line", "[viewmodel][support][golden]")
{
    const auto difference = test::compare_golden("prd-example", "Facilitator: Someone Else\n");

    REQUIRE(difference.has_value());
    CHECK(difference->find("line 1") != std::string::npos);
}

TEST_CASE("The fixed zones give Melbourne +11:00 on 7 November 2026", "[viewmodel][support][golden]")
{
    const auto zones = test::make_fixed_zones();

    const auto local = zones.to_local({test::utc(2026, 11, 7, 0, 5), "Australia/Melbourne"});

    REQUIRE(local.has_value());
    CHECK(local->m_SinceMidnight == std::chrono::hours(11) + std::chrono::minutes(5));
    CHECK_FALSE(zones.to_local({test::utc(2026, 11, 7, 0, 5), "Nowhere/Known"}).has_value());
}
