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
    header.m_Facilitator = "Sam Example";
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
    const auto header = render_header(test::prd_example_log(), test::prd_example_context());
    REQUIRE(header.has_value());
    CHECK(*header
          == "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\nLocation: North Site\n"
             "Software Version: 1.4.2");
}

TEST_CASE("An empty header field prints its label and a colon with no trailing space", "[export][header]")
{
    const auto header = render_header(Log(sample_header()), demo_context());
    REQUIRE(header.has_value());
    CHECK(*header == "Facilitator: Sam Example\nDate: 7 Nov 2026\nDEMO #:\nLocation:\nSoftware Version:");
}

TEST_CASE("The header uses the prefix, free-text locations and single-digit days", "[export][header]")
{
    auto values = sample_header();
    values.m_LocalDate = std::chrono::year{2026} / 4 / 5;
    values.m_UnitNumber = UnitNumber::parse("9").value();
    values.m_Location = LocationValue{std::nullopt, "Back Room"};
    const auto header = render_header(Log(values), demo_context());
    REQUIRE(header.has_value());
    CHECK(*header == "Facilitator: Sam Example\nDate: 5 Apr 2026\nDEMO #: DEMO-9\nLocation: Back Room\nSoftware Version:");
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
    CHECK(render_export(Log(values), ExportContext{"DEMO", nullptr}).error() == RenderError::UNKNOWN_LOCATION);
}

TEST_CASE("The header block of the PRD example is the first five lines of the golden", "[export][golden]")
{
    const auto golden = test::read_bytes(std::filesystem::path(EJ_GOLDEN_DIR) / "prd-example.txt");
    REQUIRE(golden.has_value());
    const auto header = render_header(test::prd_example_log(), test::prd_example_context());
    REQUIRE(header.has_value());
    CHECK(golden->starts_with(*header + "\n\n"));
}

// Work package 1.5 adds the timeline and removes the [!shouldfail] tag, so this test then has to pass.
TEST_CASE("The PRD example export matches the golden byte for byte", "[export][golden][!shouldfail]")
{
    const auto text = render_export(test::prd_example_log(), test::prd_example_context());
    REQUIRE(text.has_value());
    test::check_golden("prd-example", *text);
}
