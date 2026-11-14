// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"
#include "golden.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ej;
using ej::test::test_id;

namespace
{

HeaderValues sample_header()
{
    HeaderValues header;
    header.m_Tester = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    return header;
}

ExportContext demo_context()
{
    ExportContext context;
    context.m_UnitPrefix = "DEMO";
    context.m_LocationName = [](LocationId) { return std::string_view("North Site"); };
    return context;
}

}  // namespace

TEST_CASE("The header block shows all five lines, values at the start of the log", "[export][header]")
{
    const auto scenario = test::make_scenario("prd-example");
    const auto header = render_header(scenario.m_Log, scenario.m_Context);
    REQUIRE(header.has_value());
    CHECK(*header
          == "Tester: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\nLocation: North Site\n"
             "Software Version: v1.4.2");
}

TEST_CASE("An empty header field prints its label and a colon with no trailing space", "[export][header]")
{
    const auto header = render_header(Log(sample_header()), demo_context());
    REQUIRE(header.has_value());
    CHECK(*header == "Tester: Sam Example\nDate: 7 Nov 2026\nDEMO #:\nLocation:\nSoftware Version:");
}

TEST_CASE("The header uses the prefix, free-text locations and single-digit days", "[export][header]")
{
    auto values = sample_header();
    values.m_LocalDate = std::chrono::year{2026} / 4 / 5;
    values.m_UnitNumber = UnitNumber::parse("9").value();
    values.m_Location = LocationValue{std::nullopt, "Back Room"};
    const auto header = render_header(Log(values), demo_context());
    REQUIRE(header.has_value());
    CHECK(*header == "Tester: Sam Example\nDate: 5 Apr 2026\nDEMO #: DEMO-9\nLocation: Back Room\nSoftware Version:");
}

TEST_CASE("An unknown location id is an error and renders nothing", "[export][header]")
{
    auto values = sample_header();
    values.m_Location = LocationValue{test_id<LocationId>(99), {}};
    ExportContext context = demo_context();
    context.m_LocationName = [](LocationId) { return std::string_view(); };
    const auto header = render_header(Log(values), context);
    REQUIRE_FALSE(header.has_value());
    CHECK(header.error() == RenderError::UNKNOWN_LOCATION);
    CHECK(render_export(Log(values), ExportContext{}).error() == RenderError::UNKNOWN_LOCATION);
}

TEST_CASE("The header block of the PRD example is the first five lines of the golden", "[export][golden]")
{
    const auto golden = test::read_bytes(std::filesystem::path(EJ_GOLDEN_DIR) / "prd-example.txt");
    REQUIRE(golden.has_value());
    const auto scenario = test::make_scenario("prd-example");
    const auto header = render_header(scenario.m_Log, scenario.m_Context);
    REQUIRE(header.has_value());
    CHECK(golden->starts_with(*header + "\n\n"));
}

TEST_CASE("The header block and a change line show the environment of the unit", "[export][header][environment]")
{
    auto values = sample_header();
    values.m_UnitNumber = UnitNumber::parse("27", "prod").value();
    const auto header = render_header(Log(values), demo_context());
    REQUIRE(header.has_value());
    CHECK(*header == "Tester: Sam Example\nDate: 7 Nov 2026\nDEMO #: DEMO-27-prod\nLocation:\nSoftware Version:");

    // A unit without one prints as it always did.
    values.m_UnitNumber = UnitNumber::parse("27").value();
    CHECK(render_header(Log(values), demo_context()).value().find("DEMO #: DEMO-27\n") != std::string::npos);
}

TEST_CASE("The environment golden prints the header and the lines of the changes", "[export][golden][environment]")
{
    const auto scenario = test::make_scenario("unit-environment");
    const auto text = render_export(scenario.m_Log, scenario.m_Context);
    REQUIRE(text.has_value());
    test::check_golden("unit-environment", *text);
}

TEST_CASE("The header block prints the commit after the version, in brackets", "[export][header][commit]")
{
    auto values = sample_header();
    values.m_SoftwareVersion = SoftwareVersion::parse("1.4.2", "a1b2c3d").value();
    const auto header = render_header(Log(values), demo_context());
    REQUIRE(header.has_value());
    CHECK(*header ==
          "Tester: Sam Example\nDate: 7 Nov 2026\nDEMO #:\nLocation:\nSoftware Version: v1.4.2 (a1b2c3d)");

    // With no commit the text is exactly what it was.
    values.m_SoftwareVersion = SoftwareVersion::parse("1.4.2").value();
    CHECK(render_header(Log(values), demo_context()).value() ==
          "Tester: Sam Example\nDate: 7 Nov 2026\nDEMO #:\nLocation:\nSoftware Version: v1.4.2");
}

TEST_CASE("The commit golden prints the header and the lines of the changes", "[export][golden][commit]")
{
    const auto scenario = test::make_scenario("version-commit");
    const auto text = render_export(scenario.m_Log, scenario.m_Context);
    REQUIRE(text.has_value());
    test::check_golden("version-commit", *text);
}
