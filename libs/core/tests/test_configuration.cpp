// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/configuration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>

using ej::Configuration;
using ej::ConfigError;
using Code = ej::ConfigError::Code;

namespace
{

std::string read_file(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::string sample()
{
    return read_file(EJ_SAMPLE_CONFIG);
}

/// Replaces the first occurrence of `from`; the test fails if the sample does not contain it.
std::string edited(std::string text, const std::string& from, const std::string& to)
{
    const auto at = text.find(from);
    REQUIRE(at != std::string::npos);
    text.replace(at, from.size(), to);
    return text;
}

std::vector<ConfigError> errors_of(const std::string& json)
{
    const auto result = ej::import_configuration(json);
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

bool has(const std::vector<ConfigError>& errors, Code code, const std::string& path)
{
    return std::any_of(errors.begin(), errors.end(),
                       [&](const ConfigError& e) { return e.m_Code == code && e.m_Path == path; });
}

Configuration imported(const std::string& json)
{
    auto result = ej::import_configuration(json);
    if (!result)
    {
        INFO(result.error().front().m_Message);
    }
    REQUIRE(result.has_value());
    return *result;
}

}  // namespace

TEST_CASE("The sample configuration imports and exports byte for byte", "[config]")
{
    const auto text = sample();
    const auto configuration = imported(text);
    CHECK(configuration.m_Facilitator == "Sam Example");
    CHECK(configuration.m_UnitPrefix == "UNIT");
    CHECK(configuration.m_ErrorTypes.size() == 11);
    CHECK(configuration.m_Locations.size() == 2);
    CHECK(configuration.m_Templates.size() == 2);
    CHECK(configuration.m_ErrorTypes[0].m_Name == "Wrong Response");
    CHECK(ej::export_configuration(configuration) == text);
}

TEST_CASE("Import of an export equals the original, with unicode, retired types and all clock formats", "[config]")
{
    Configuration c;
    c.m_Facilitator = "Sam Éxample 日本 \"quoted\" \\ back\tslash";
    c.m_UnitPrefix = "UNIT";
    c.m_IncludeTotals = true;
    c.m_ErrorTypes.push_back({ej::ErrorTypeId{ej::Uuid::from_random(1, 1)}, "Wrong Response", false, 3});
    c.m_ErrorTypes.push_back({ej::ErrorTypeId{ej::Uuid::from_random(2, 2)}, "Old type", true, 3});
    c.m_ErrorTypes.push_back({ej::ErrorTypeId{ej::Uuid::from_random(3, 3)}, "Off the board", false, std::nullopt});
    c.m_Locations.push_back({ej::LocationId{ej::Uuid::from_random(4, 4)}, "North Site – ünïcode", 2, false});
    c.m_Locations.push_back({ej::LocationId{ej::Uuid::from_random(5, 5)}, "Gone", -1, true});
    c.m_Templates.push_back({ej::NoteTemplateId{ej::Uuid::from_random(6, 6)}, "Played song", "Which song?",
                             "Played {value} {{twice}}", 1});

    for (const bool twelve : {false, true})
    {
        for (const bool seconds : {false, true})
        {
            for (const bool lower : {false, true})
            {
                c.m_TimeFormat.m_Style = twelve ? ej::TimeStyle::H12 : ej::TimeStyle::H24;
                c.m_TimeFormat.m_Seconds = seconds;
                c.m_TimeFormat.m_Meridiem =
                    lower ? ej::MeridiemCase::LOWER : ej::MeridiemCase::UPPER;
                const auto text = ej::export_configuration(c);
                const auto back = imported(text);
                CHECK(back.m_Facilitator == c.m_Facilitator);
                CHECK(back.m_TimeFormat == c.m_TimeFormat);
                CHECK(back.m_IncludeTotals == c.m_IncludeTotals);
                CHECK(back.m_ErrorTypes == c.m_ErrorTypes);
                CHECK(back.m_Locations == c.m_Locations);
                REQUIRE(back.m_Templates.size() == 1);
                CHECK(back.m_Templates[0].m_Id == c.m_Templates[0].m_Id);
                CHECK(back.m_Templates[0].m_OutputPattern == c.m_Templates[0].m_OutputPattern);
                CHECK(ej::export_configuration(back) == text);
            }
        }
    }
}

TEST_CASE("A retired type keeps its board position and does not clash with a live one", "[config]")
{
    // The second type is retired and shares position 3 with the first; the layout ignores it.
    Configuration c;
    c.m_Facilitator = "Sam Example";
    c.m_UnitPrefix = "UNIT";
    c.m_ErrorTypes.push_back({ej::ErrorTypeId{ej::Uuid::from_random(1, 1)}, "Wrong Response", false, 3});
    c.m_ErrorTypes.push_back({ej::ErrorTypeId{ej::Uuid::from_random(2, 2)}, "wrong response", true, 3});
    const auto back = imported(ej::export_configuration(c));
    CHECK(back.m_ErrorTypes[1].m_BoardPosition == 3);
    CHECK(back.m_ErrorTypes[1].m_Retired);
}

TEST_CASE("Export has a fixed layout", "[config]")
{
    const auto text = ej::export_configuration(Configuration{});
    CHECK(text.starts_with("{\n  \"schema_version\": 1,\n  \"preferences\": {\n"));
    CHECK(text.find('\r') == std::string::npos);
    CHECK(text.ends_with("  \"error_types\": [],\n  \"locations\": [],\n  \"note_templates\": []\n}\n"));
}

TEST_CASE("A byte-order mark is accepted", "[config]")
{
    CHECK(ej::import_configuration("\xEF\xBB\xBF" + sample()).has_value());
}

TEST_CASE("Files that are not configurations are rejected", "[config]")
{
    CHECK(errors_of("this is not json").front().m_Code == Code::MALFORMED_JSON);
    CHECK(errors_of("").front().m_Code == Code::MALFORMED_JSON);
    CHECK(errors_of("{\"schema_version\": 1,").front().m_Code == Code::MALFORMED_JSON);
    CHECK(errors_of("[1, 2]").front().m_Code == Code::WRONG_TYPE);
    CHECK(errors_of("42").front().m_Code == Code::MALFORMED_JSON);
}

TEST_CASE("The schema version is checked first", "[config]")
{
    CHECK(has(errors_of("{}"), Code::MISSING_FIELD, "/schema_version"));

    const auto future = errors_of(edited(sample(), "\"schema_version\": 1", "\"schema_version\": 2"));
    REQUIRE(future.size() == 1);
    CHECK(future[0].m_Code == Code::UNSUPPORTED_VERSION);
    CHECK(future[0].m_Path == "/schema_version");
    CHECK(future[0].m_Message.find("Update the app") != std::string::npos);

    CHECK(has(errors_of(edited(sample(), "\"schema_version\": 1", "\"schema_version\": 0")), Code::OUT_OF_RANGE,
              "/schema_version"));
    CHECK(has(errors_of(edited(sample(), "\"schema_version\": 1", "\"schema_version\": \"1\"")), Code::WRONG_TYPE,
              "/schema_version"));
    CHECK(has(errors_of(edited(sample(), "\"schema_version\": 1", "\"schema_version\": 1.5")), Code::WRONG_TYPE,
              "/schema_version"));
    CHECK(has(errors_of(edited(sample(), "\"schema_version\": 1", "\"schema_version\": 1e300")),
              Code::UNSUPPORTED_VERSION, "/schema_version"));
}

TEST_CASE("Duplicate ids and names are rejected", "[config]")
{
    SECTION("ids")
    {
        const auto id = std::string{"00000000-0000-4000-8000-0000000000a1"};
        const auto e = errors_of(edited(sample(), "00000000-0000-4000-8000-0000000000a2", id));
        CHECK(has(e, Code::DUPLICATE, "/error_types/1/id"));
    }
    SECTION("error type names, ignoring case")
    {
        const auto e = errors_of(edited(sample(), "\"Sequence Out of Order\"", "\"WRONG response\""));
        CHECK(has(e, Code::DUPLICATE, "/error_types/1/name"));
    }
    SECTION("location and template names")
    {
        CHECK(has(errors_of(edited(sample(), "\"South Site\"", "\"north site\"")), Code::DUPLICATE,
                  "/locations/1/name"));
        CHECK(has(errors_of(edited(sample(), "\"Visitor interaction\"", "\"Played song\"")), Code::DUPLICATE,
                  "/note_templates/1/name"));
    }
}

TEST_CASE("Board positions are 1 to 11 and unique among live types", "[config]")
{
    CHECK(has(errors_of(edited(sample(), "\"board_position\": 4", "\"board_position\": 3")), Code::DUPLICATE,
              "/error_types/3/board_position"));
    CHECK(has(errors_of(edited(sample(), "\"board_position\": 4", "\"board_position\": 0")), Code::OUT_OF_RANGE,
              "/error_types/3/board_position"));
    CHECK(has(errors_of(edited(sample(), "\"board_position\": 4", "\"board_position\": 12")), Code::OUT_OF_RANGE,
              "/error_types/3/board_position"));
    CHECK(has(errors_of(edited(sample(), "\"board_position\": 4", "\"board_position\": 4.5")), Code::WRONG_TYPE,
              "/error_types/3/board_position"));
    CHECK(has(errors_of(edited(sample(), "\"board_position\": 4", "\"board_position\": \"4\"")), Code::WRONG_TYPE,
              "/error_types/3/board_position"));
}

TEST_CASE("Ids must be UUIDs", "[config]")
{
    const auto old_id = std::string{"00000000-0000-4000-8000-0000000000b1"};
    CHECK(has(errors_of(edited(sample(), old_id, "not-a-uuid")), Code::BAD_ID, "/locations/0/id"));
    CHECK(has(errors_of(edited(sample(), old_id, "00000000-0000-0000-0000-000000000000")), Code::BAD_ID,
              "/locations/0/id"));
    CHECK(has(errors_of(edited(sample(), "\"id\": \"" + old_id + "\"", "\"id\": 7")), Code::WRONG_TYPE,
              "/locations/0/id"));
    CHECK(has(errors_of(edited(sample(), "\"id\": \"" + old_id + "\",", "")), Code::MISSING_FIELD,
              "/locations/0/id"));
}

TEST_CASE("Patterns, facilitator and prefix are validated", "[config]")
{
    CHECK(has(errors_of(edited(sample(), "Played {value}", "Played")), Code::BAD_PATTERN,
              "/note_templates/0/output_pattern"));
    CHECK(has(errors_of(edited(sample(), "Played {value}", "Played {value")), Code::BAD_PATTERN,
              "/note_templates/0/output_pattern"));
    CHECK(has(errors_of(edited(sample(), "Played {value}", "Played {name}")), Code::BAD_PATTERN,
              "/note_templates/0/output_pattern"));
    CHECK(has(errors_of(edited(sample(), "\"Sam Example\"", "\"\"")), Code::OUT_OF_RANGE, "/preferences/facilitator"));
    CHECK(has(errors_of(edited(sample(), "\"Sam Example\"", "\"   \"")), Code::OUT_OF_RANGE,
              "/preferences/facilitator"));
    CHECK(has(errors_of(edited(sample(), "\"UNIT\"", "\"UNIT-\"")), Code::OUT_OF_RANGE, "/preferences/unit_prefix"));
    CHECK(has(errors_of(edited(sample(), "\"UNIT\"", "\"UNIT #\"")), Code::OUT_OF_RANGE, "/preferences/unit_prefix"));
    CHECK(has(errors_of(edited(sample(), "\"UNIT\"", "\"\"")), Code::OUT_OF_RANGE, "/preferences/unit_prefix"));
    CHECK(has(errors_of(edited(sample(), "\"facilitator\": \"Sam Example\",", "")), Code::MISSING_FIELD,
              "/preferences/facilitator"));
}

TEST_CASE("Several faults are all reported, in file order", "[config]")
{
    auto text = edited(sample(), "\"Sam Example\"", "\"\"");
    text = edited(text, "\"Sequence Out of Order\"", "\"Wrong Response\"");
    text = edited(text, "00000000-0000-4000-8000-0000000000b2", "oops");
    text = edited(text, "Visitor: {value}", "Visitor");

    const auto errors = errors_of(text);
    std::vector<std::string> paths;
    for (const auto& e : errors)
    {
        paths.push_back(e.m_Path);
        CHECK_FALSE(e.m_Message.empty());
        CHECK(e.m_Message.find(e.m_Path) != std::string::npos);
    }
    const std::vector<std::string> expected{"/preferences/facilitator", "/error_types/1/name", "/locations/1/id",
                                            "/note_templates/1/output_pattern"};
    CHECK(paths == expected);
}

TEST_CASE("Unknown keys are rejected", "[config]")
{
    CHECK(has(errors_of(edited(sample(), "\"schema_version\": 1,", "\"schema_version\": 1, \"extra\": 1,")),
              Code::UNKNOWN_FIELD, "/extra"));
    CHECK(has(errors_of(edited(sample(), "\"unit_prefix\"", "\"unitPrefix\": \"X\", \"unit_prefix\"")),
              Code::UNKNOWN_FIELD, "/preferences/unitPrefix"));
    CHECK(has(errors_of(edited(sample(), "\"retired\": false,\n      \"board_position\": 1",
                               "\"retired\": false, \"tileColour\": \"#fff\",\n      \"board_position\": 1")),
              Code::UNKNOWN_FIELD, "/error_types/0/tileColour"));
    CHECK(has(errors_of(edited(sample(), "\"seconds\": false", "\"seconds\": false, \"zone\": 1")),
              Code::UNKNOWN_FIELD, "/preferences/time_format/zone"));
    CHECK(has(errors_of(edited(sample(), "\"schema_version\": 1,", "\"schema_version\": 1, \"a/b~c\": 1,")),
              Code::UNKNOWN_FIELD, "/a~1b~0c"));
}

TEST_CASE("The clock format has defaults and rejects other values", "[config]")
{
    const auto without_meridiem = edited(sample(), ",\n      \"meridiem\": \"upper\"", "");
    CHECK(imported(without_meridiem).m_TimeFormat.m_Meridiem == ej::MeridiemCase::UPPER);

    const auto twelve_lower = edited(edited(sample(), "\"24h\"", "\"12h\""), "\"upper\"", "\"lower\"");
    const auto c = imported(twelve_lower);
    CHECK(c.m_TimeFormat.m_Style == ej::TimeStyle::H12);
    CHECK(c.m_TimeFormat.m_Meridiem == ej::MeridiemCase::LOWER);

    CHECK(has(errors_of(edited(sample(), "\"24h\"", "\"36h\"")), Code::OUT_OF_RANGE, "/preferences/time_format/style"));
    CHECK(has(errors_of(edited(sample(), "\"upper\"", "\"Title\"")), Code::OUT_OF_RANGE,
              "/preferences/time_format/meridiem"));
    CHECK(has(errors_of(edited(sample(), "\"seconds\": false", "\"seconds\": \"no\"")), Code::WRONG_TYPE,
              "/preferences/time_format/seconds"));
    CHECK(has(errors_of(edited(sample(), "\"include_totals\": false", "\"include_totals\": 0")), Code::WRONG_TYPE,
              "/preferences/include_totals"));
}

TEST_CASE("Wrong shapes are reported without stopping", "[config]")
{
    CHECK(has(errors_of(edited(sample(), "\"locations\": [", "\"locations\": {}, \"x\": [")), Code::WRONG_TYPE,
              "/locations"));
    CHECK(has(errors_of(edited(sample(), "\"preferences\": {", "\"preferences\": [], \"x\": {")), Code::WRONG_TYPE,
              "/preferences"));
    CHECK(has(errors_of("{\"schema_version\": 1}"), Code::MISSING_FIELD, "/preferences"));
    CHECK(has(errors_of("{\"schema_version\": 1}"), Code::MISSING_FIELD, "/note_templates"));
    CHECK(has(errors_of(edited(sample(), "\"locations\": [\n    {", "\"locations\": [\n    7, {")), Code::WRONG_TYPE,
              "/locations/0"));
}

TEST_CASE("Limits stop oversized files", "[config]")
{
    SECTION("over 1 MiB, before parsing")
    {
        std::string big = sample();
        big.append(ej::CONFIG_MAX_BYTES, ' ');
        const auto e = errors_of(big);
        REQUIRE(e.size() == 1);
        CHECK(e[0].m_Code == Code::OUT_OF_RANGE);
        CHECK(e[0].m_Path.empty());
    }
    SECTION("exactly 1 MiB is not too big")
    {
        std::string padded = sample();
        padded.append(ej::CONFIG_MAX_BYTES - padded.size(), ' ');
        CHECK(ej::import_configuration(padded).has_value());
    }
    SECTION("500 error types pass, 501 do not")
    {
        Configuration c;
        c.m_Facilitator = "Sam Example";
        c.m_UnitPrefix = "UNIT";
        for (std::uint64_t i = 1; i <= ej::CONFIG_MAX_ENTRIES_PER_LIST; ++i)
        {
            c.m_ErrorTypes.push_back(
                {ej::ErrorTypeId{ej::Uuid::from_random(i, i)}, "Type " + std::to_string(i), false, std::nullopt});
        }
        CHECK(ej::import_configuration(ej::export_configuration(c)).has_value());
        c.m_ErrorTypes.push_back({ej::ErrorTypeId{ej::Uuid::from_random(999, 999)}, "One too many", false, std::nullopt});
        const auto e = errors_of(ej::export_configuration(c));
        REQUIRE(e.size() == 1);
        CHECK(e[0].m_Code == Code::OUT_OF_RANGE);
        CHECK(e[0].m_Path == "/error_types");
    }
}

TEST_CASE("Messages never echo a long value", "[config]")
{
    const std::string long_value(120, 'z');
    const auto errors = errors_of(edited(sample(), "\"24h\"", "\"" + long_value + "\""));
    REQUIRE_FALSE(errors.empty());
    for (const auto& e : errors)
    {
        CHECK(e.m_Message.find(long_value) == std::string::npos);
    }
    CHECK(has(errors, Code::OUT_OF_RANGE, "/preferences/time_format/style"));
}
