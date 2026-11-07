// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/header_history.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr const char* HEADER =
    "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\nLocation: North Site\nSoftware Version: 1.4.2";

void must_add(Log& log, Entry entry)
{
    REQUIRE(log.add(std::move(entry)).has_value());
}

std::string render(const Scenario& scenario)
{
    const auto text = render_export(scenario.m_Log, scenario.m_Context);
    REQUIRE(text.has_value());
    return *text;
}

// The en dash and the arrow as UTF-8.
constexpr const char* DASH = "\xE2\x80\x93";
constexpr const char* ARROW = "\xE2\x86\x92";

}  // namespace

TEST_CASE("Rule 1: the header block holds the start values and the log's date, whatever the entries say", "[export][rule1]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    REQUIRE(change_header(scenario.m_Log, HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(11, 0), EntryId{}).has_value());
    // An entry on the next day does not change the Date line.
    Timestamp next_day = on_day(10, 0);
    next_day.m_Utc += std::chrono::hours(24);
    must_add(scenario.m_Log, error_entry(2, next_day, ERROR_MUTE_DELAY));
    const auto text = render(scenario);
    CHECK(text.starts_with(std::string(HEADER) + "\n\n"));
}

TEST_CASE("Rule 2: an empty header field prints its label and a colon only", "[export][rule2]")
{
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    auto scenario = blank_scenario();
    scenario.m_Log = Log(header);
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    CHECK(render(scenario)
          == "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #:\nLocation:\nSoftware Version:\n\n10:05 T| Wrong Response");
}

TEST_CASE("Rule 3: an error line, with an attached note after an en dash", "[export][rule3]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    must_add(scenario.m_Log, error_entry(2, on_day(10, 6), ERROR_MUTE_DELAY, "asked for Evening Song"));
    must_add(scenario.m_Log, error_entry(3, on_day(10, 7), ERROR_SPEECH_DELAY, "   "));  // blank note: no dash
    CHECK(render(scenario)
          == std::string(HEADER) + "\n\n10:05 T| Wrong Response\n10:06 T| Mute Delay " + DASH
                 + " asked for Evening Song\n10:07 T| Speech Delay");
}

TEST_CASE("Rule 4: a note line is (Free Text) and the rendered template or the free text", "[export][rule4]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, template_note_entry(1, on_day(10, 5), TEMPLATE_PLAYED, "Morning Medley"));
    must_add(scenario.m_Log, free_note_entry(2, on_day(10, 6), "Robot restarted"));
    must_add(scenario.m_Log, free_note_entry(3, on_day(10, 7), ""));
    CHECK(render(scenario)
          == std::string(HEADER)
                 + "\n\n10:05 (Free Text) Played Morning Medley\n10:06 (Free Text) Robot restarted\n10:07 (Free Text)");
}

TEST_CASE("Rule 5: a header-change line names the field with its old and new value", "[export][rule5]")
{
    auto scenario = blank_scenario();
    REQUIRE(change_header(scenario.m_Log, HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(10, 0), test_id<EntryId>(1)));
    REQUIRE(change_header(scenario.m_Log, HeaderField::UNIT_NUMBER, "31", on_day(11, 0), test_id<EntryId>(2)));
    REQUIRE(change_header(scenario.m_Log, HeaderField::LOCATION, test_id<LocationId>(LOCATION_SOUTH_SITE).format(),
                          on_day(12, 0), test_id<EntryId>(3)));
    REQUIRE(change_header(scenario.m_Log, HeaderField::LOCATION, "Back Room", on_day(13, 0), test_id<EntryId>(4)));
    CHECK(render(scenario)
          == std::string(HEADER) + "\n\n10:00 Software Version changed: 1.4.2 " + ARROW + " 1.5.0\n11:00 UNIT # changed: UNIT-27 "
                 + ARROW + " UNIT-31\n12:00 Location changed: North Site " + ARROW + " South Site\n13:00 Location changed: South Site "
                 + ARROW + " Back Room");

    scenario.m_Context.m_UnitPrefix = "DEMO";
    CHECK(render(scenario).find("11:00 DEMO # changed: DEMO-27 ") != std::string::npos);
}

TEST_CASE("Rule 5: a first change from an empty field has an empty old side", "[export][rule5]")
{
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    auto scenario = blank_scenario();
    scenario.m_Log = Log(header);
    REQUIRE(change_header(scenario.m_Log, HeaderField::SOFTWARE_VERSION, "1.4.2", on_day(10, 0), test_id<EntryId>(1)));
    const auto line = render_entry_line(scenario.m_Log, test_id<EntryId>(1), scenario.m_Context);
    REQUIRE(line.has_value());
    CHECK(*line == std::string("10:00 Software Version changed:  ") + ARROW + " 1.4.2");
}

TEST_CASE("Rule 6: each entry is shown in the zone it was captured in, in the chosen clock format", "[export][rule6]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    must_add(scenario.m_Log, error_entry(2, on_day(8, 10, 0, 0, "Australia/Perth"), ERROR_MUTE_DELAY));
    must_add(scenario.m_Log, error_entry(3, on_day(0, 20, 0, 0, "UTC"), ERROR_SPEECH_DELAY));
    CHECK(render(scenario)
          == std::string(HEADER) + "\n\n10:05 T| Wrong Response\n08:10 T| Mute Delay\n00:20 T| Speech Delay");

    scenario.m_Context.m_TimeFormat = TimeFormat{TimeStyle::H12, true, MeridiemCase::LOWER};
    CHECK(render(scenario)
          == std::string(HEADER) + "\n\n10:05:00 am T| Wrong Response\n08:10:00 am T| Mute Delay\n12:20:00 am T| Speech Delay");
}

TEST_CASE("Rule 7: deleted entries are skipped, and totals follow a blank line when on", "[export][rule7]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    must_add(scenario.m_Log, error_entry(2, on_day(10, 6), ERROR_MUTE_DELAY));
    must_add(scenario.m_Log, error_entry(3, on_day(10, 7), ERROR_WRONG_RESPONSE));
    REQUIRE(scenario.m_Log.set_deleted(test_id<EntryId>(2), true).has_value());
    CHECK(render(scenario) == std::string(HEADER) + "\n\n10:05 T| Wrong Response\n10:07 T| Wrong Response");

    scenario.m_Context.m_IncludeTotals = true;
    CHECK(render(scenario)
          == std::string(HEADER) + "\n\n10:05 T| Wrong Response\n10:07 T| Wrong Response\n\nTotals: Wrong Response 2");
}

TEST_CASE("Rule 7: totals are in the declared order, zero counts are left out, unlisted types come last", "[export][rule7]")
{
    auto scenario = blank_scenario();
    scenario.m_Context.m_IncludeTotals = true;
    scenario.m_Context.m_TotalsOrder = {test_id<ErrorTypeId>(ERROR_SPEECH_DELAY), test_id<ErrorTypeId>(ERROR_MUTE_DELAY),
                                        test_id<ErrorTypeId>(ERROR_LATE_GREETING)};
    must_add(scenario.m_Log, error_entry(1, on_day(10, 1), ERROR_WRONG_RESPONSE));  // not in the order: last
    must_add(scenario.m_Log, error_entry(2, on_day(10, 2), ERROR_MUTE_DELAY));
    must_add(scenario.m_Log, error_entry(3, on_day(10, 3), ERROR_SPEECH_DELAY));
    must_add(scenario.m_Log, error_entry(4, on_day(10, 4), ERROR_SPEECH_DELAY));
    must_add(scenario.m_Log, error_entry(5, on_day(10, 5), ERROR_WRONG_TRACK_PLAYED));  // also not listed
    const auto text = render(scenario);
    CHECK(text.ends_with("\n\nTotals: Speech Delay 2, Mute Delay 1, Wrong Response 1, Wrong Track Played 1"));
}

TEST_CASE("Rule 7: no totals line when there are no errors", "[export][rule7]")
{
    auto scenario = blank_scenario();
    scenario.m_Context.m_IncludeTotals = true;
    CHECK(render(scenario) == HEADER);
    must_add(scenario.m_Log, free_note_entry(1, on_day(10, 5), "A note"));
    CHECK(render(scenario) == std::string(HEADER) + "\n\n10:05 (Free Text) A note");
}

TEST_CASE("Rule 8: header, a blank line, the timeline; LF only and no trailing newline", "[export][rule8]")
{
    auto scenario = blank_scenario();
    CHECK(render(scenario) == HEADER);  // an empty log is the header block alone
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    const auto text = render(scenario);
    CHECK(text == std::string(HEADER) + "\n\n10:05 T| Wrong Response");
    CHECK(text.find('\r') == std::string::npos);
    CHECK_FALSE(text.ends_with('\n'));
}

TEST_CASE("Rule 9: line breaks and runs of white space in free text collapse to single spaces", "[export][rule9]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, free_note_entry(1, on_day(10, 1), "one\ntwo\r\n\r\nthree"));
    must_add(scenario.m_Log, free_note_entry(2, on_day(10, 2), "tab\there  and   spaces   "));
    must_add(scenario.m_Log, error_entry(3, on_day(10, 3), ERROR_MUTE_DELAY, "  note\nwith\tbreaks  "));
    must_add(scenario.m_Log, template_note_entry(4, on_day(10, 4), TEMPLATE_PLAYED, "Two\nLines"));
    must_add(scenario.m_Log, free_note_entry(5, on_day(10, 5), std::string("next\xC2\x85line\xE2\x80\xA8sep")));
    CHECK(render(scenario)
          == std::string(HEADER)
                 + "\n\n10:01 (Free Text) one two three\n10:02 (Free Text) tab here and spaces\n10:03 T| Mute Delay " + DASH
                 + " note with breaks\n10:04 (Free Text) Played Two Lines\n10:05 (Free Text) next line sep");
}

TEST_CASE("Rule 9: the header block is one line per field too", "[export][rule9]")
{
    HeaderValues header;
    header.m_Facilitator = "Sam\nExample ";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    header.m_Location = LocationValue{std::nullopt, "North\tSite\n"};
    auto scenario = blank_scenario();
    scenario.m_Log = Log(header);
    CHECK(render(scenario) == "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #:\nLocation: North Site\nSoftware Version:");
}

TEST_CASE("render_entry_line of every entry equals the matching line of render_export", "[export]")
{
    for (const auto name : golden_names())
    {
        DYNAMIC_SECTION(name)
        {
            const auto scenario = make_scenario(name);
            const auto whole = render(scenario);
            std::vector<std::string> lines;
            for (std::size_t start = 0; start <= whole.size();)
            {
                const auto end = whole.find('\n', start);
                lines.push_back(whole.substr(start, end == std::string::npos ? end : end - start));
                if (end == std::string::npos)
                {
                    break;
                }
                start = end + 1;
            }
            for (const Entry* entry : scenario.m_Log.sorted_entries(false))
            {
                const auto line = render_entry_line(scenario.m_Log, entry->m_Id, scenario.m_Context);
                REQUIRE(line.has_value());
                CHECK(std::find(lines.begin(), lines.end(), *line) != lines.end());
            }
        }
    }
}

TEST_CASE("render_entry_line of a deleted or unknown entry", "[export]")
{
    auto scenario = blank_scenario();
    must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
    REQUIRE(scenario.m_Log.set_deleted(test_id<EntryId>(1), true).has_value());
    CHECK(render_entry_line(scenario.m_Log, test_id<EntryId>(1), scenario.m_Context).value() == "10:05 T| Wrong Response");
    CHECK(render_entry_line(scenario.m_Log, test_id<EntryId>(9), scenario.m_Context).error() == RenderError::UNKNOWN_ENTRY);
}

TEST_CASE("An unknown error type, location, template or zone is an error and renders nothing", "[export]")
{
    auto scenario = blank_scenario();

    SECTION("error type")
    {
        must_add(scenario.m_Log, error_entry(1, on_day(10, 5), 99));
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_ERROR_TYPE);
    }
    SECTION("error type in the totals")
    {
        scenario.m_Context.m_IncludeTotals = true;
        must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
        scenario.m_Context.m_ErrorTypeName = [](ErrorTypeId) { return std::string_view(); };
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_ERROR_TYPE);
    }
    SECTION("template")
    {
        must_add(scenario.m_Log, template_note_entry(1, on_day(10, 5), 99, "x"));
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_TEMPLATE);
    }
    SECTION("location in a change line")
    {
        REQUIRE(change_header(scenario.m_Log, HeaderField::LOCATION, test_id<LocationId>(99).format(), on_day(10, 5),
                              test_id<EntryId>(1)));
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_LOCATION);
    }
    SECTION("zone")
    {
        must_add(scenario.m_Log, error_entry(1, on_day(10, 5, 0, 0, "Nowhere/Land"), ERROR_WRONG_RESPONSE));
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_ZONE);
    }
    SECTION("no zone provider")
    {
        must_add(scenario.m_Log, error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE));
        scenario.m_Context.m_Zones = nullptr;
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_ZONE);
    }
    SECTION("no lookups at all")
    {
        must_add(scenario.m_Log, template_note_entry(1, on_day(10, 5), TEMPLATE_PLAYED, "x"));
        scenario.m_Context.m_Template = nullptr;
        CHECK(render_export(scenario.m_Log, scenario.m_Context).error() == RenderError::UNKNOWN_TEMPLATE);
    }
}

TEST_CASE("Rendering is deterministic, leaves the log alone and uses only printable text", "[export]")
{
    for (const auto name : golden_names())
    {
        DYNAMIC_SECTION(name)
        {
            const auto scenario = make_scenario(name);
            const std::size_t entries_before = scenario.m_Log.entries().size();
            const auto first = render(scenario);
            const auto second = render(scenario);
            CHECK(first == second);
            CHECK(scenario.m_Log.entries().size() == entries_before);

            // Only printable ASCII, the en dash and the arrow (as UTF-8) and LF.
            std::string stripped = first;
            for (const char* special : {DASH, ARROW})
            {
                for (auto at = stripped.find(special); at != std::string::npos; at = stripped.find(special))
                {
                    stripped.erase(at, 3);
                }
            }
            for (const char c : stripped)
            {
                const auto u = static_cast<unsigned char>(c);
                CHECK((u == '\n' || (u >= 0x20 && u < 0x7F)));
            }
            // No line has trailing white space.
            CHECK(first.find(" \n") == std::string::npos);
            CHECK_FALSE(first.ends_with(' '));
        }
    }
}
