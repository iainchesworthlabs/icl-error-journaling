// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"
#include "golden.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

using namespace ej;

TEST_CASE("The golden export files match the renderer byte for byte", "[export][golden]")
{
    for (const auto name : test::golden_names())
    {
        DYNAMIC_SECTION(name)
        {
            const auto scenario = test::make_scenario(name);
            const auto text = render_export(scenario.m_Log, scenario.m_Context);
            REQUIRE(text.has_value());
            test::check_golden(name, *text);
        }
    }
}

TEST_CASE("Every file in golden/ has a scenario", "[export][golden]")
{
    std::vector<std::string> on_disk;
    for (const auto& item : std::filesystem::directory_iterator(EJ_GOLDEN_DIR))
    {
        if (item.path().extension() == ".txt")
        {
            on_disk.push_back(item.path().stem().string());
        }
    }
    std::erase(on_disk, "helper-selftest");  // used by the golden helper's own test
    std::vector<std::string> scenarios;
    for (const auto name : test::golden_names())
    {
        scenarios.emplace_back(name);
    }
    std::sort(on_disk.begin(), on_disk.end());
    std::sort(scenarios.begin(), scenarios.end());
    CHECK(on_disk == scenarios);
}

TEST_CASE("The PRD example is the golden prd-example.txt, byte for byte", "[export][golden]")
{
    const auto scenario = test::make_scenario("prd-example");
    const auto text = render_export(scenario.m_Log, scenario.m_Context);
    REQUIRE(text.has_value());
    const std::string prd_example =
        "Facilitator: Sam Example\n"
        "Date: 7 Nov 2026\n"
        "UNIT #: UNIT-27\n"
        "Location: North Site\n"
        "Software Version: v1.4.2\n"
        "\n"
        "10:05 T| Wrong Response\n"
        "10:20 T| Mute Delay\n"
        "10:30 (Free Text) Played Morning Medley\n"
        "11:02 T| Wrong Track Played \xE2\x80\x93 asked for Evening Song\n"
        "13:15 Software Version changed: v1.4.2 \xE2\x86\x92 v1.5.0\n"
        "13:40 T| Speech Delay";
    CHECK(*text == prd_example);
}
