// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/entry.hpp"
#include "error_journaling/export_renderer.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <variant>

using namespace ej;
using namespace ej::test;

namespace
{

Entry snapshot_entry(std::uint64_t id, Timestamp when, std::uint64_t type, std::string label)
{
    Entry entry = error_entry(id, std::move(when), type);
    std::get<ErrorEntry>(entry.m_Body).m_ErrorTypeLabel = std::move(label);
    return entry;
}

/// The configuration now calls the type "Renamed", whatever it was called at the tap.
void rename_in_configuration(Scenario& scenario)
{
    scenario.m_Context.m_ErrorTypeName = [](ErrorTypeId) -> std::string_view { return "Renamed"; };
}

std::string render(const Scenario& scenario)
{
    const auto text = render_export(scenario.m_Log, scenario.m_Context);
    REQUIRE(text.has_value());
    return *text;
}

}  // namespace

TEST_CASE("A rename after the tap does not change the export line", "[export][snapshot][unit]")
{
    auto scenario = blank_scenario();
    REQUIRE(scenario.m_Log.add(snapshot_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE, "Wrong Response")).has_value());
    rename_in_configuration(scenario);
    const auto text = render(scenario);
    CHECK(text.ends_with("10:05 T| Wrong Response"));
    CHECK(text.find("Renamed") == std::string::npos);
}

TEST_CASE("Entries made before and after a rename each read as they did when tapped", "[export][snapshot][unit]")
{
    auto scenario = blank_scenario();
    scenario.m_Context.m_IncludeTotals = true;
    REQUIRE(scenario.m_Log.add(snapshot_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE, "Wrong Response")).has_value());
    REQUIRE(scenario.m_Log.add(snapshot_entry(2, on_day(10, 20), ERROR_WRONG_RESPONSE, "Wrong Reply")).has_value());
    const auto text = render(scenario);
    CHECK(text.find("10:05 T| Wrong Response\n10:20 T| Wrong Reply") != std::string::npos);
    // One type, one count, under the name it had at its first live tap.
    CHECK(text.find("Totals: Wrong Response 2") != std::string::npos);
}

TEST_CASE("An entry without a snapshot falls back to the name looked up by id", "[export][snapshot][unit]")
{
    auto scenario = blank_scenario();
    scenario.m_Context.m_IncludeTotals = true;
    REQUIRE(scenario.m_Log.add(error_entry(1, on_day(10, 5), ERROR_WRONG_RESPONSE)).has_value());
    rename_in_configuration(scenario);
    const auto text = render(scenario);
    CHECK(text.find("10:05 T| Renamed") != std::string::npos);
    CHECK(text.find("Totals: Renamed 1") != std::string::npos);
}

TEST_CASE("An entry without a snapshot of a type the lookup does not know fails to render", "[export][snapshot][unit]")
{
    auto scenario = blank_scenario();
    REQUIRE(scenario.m_Log.add(error_entry(1, on_day(10, 5), 99)).has_value());
    const auto text = render_export(scenario.m_Log, scenario.m_Context);
    REQUIRE_FALSE(text.has_value());
    CHECK(text.error() == RenderError::UNKNOWN_ERROR_TYPE);
}

TEST_CASE("A snapshot renders even when the type is no longer known", "[export][snapshot][unit]")
{
    auto scenario = blank_scenario();
    REQUIRE(scenario.m_Log.add(snapshot_entry(1, on_day(10, 5), 99, "Gone Type")).has_value());
    CHECK(render(scenario).ends_with("10:05 T| Gone Type"));
}

TEST_CASE("A tap needs a snapshot", "[entry][snapshot][unit]")
{
    const auto type = test_id<ErrorTypeId>(ERROR_WRONG_RESPONSE);
    const auto empty = ErrorEntry::from_tap(type, "");
    REQUIRE_FALSE(empty.has_value());
    CHECK(empty.error() == ErrorEntry::Error::EMPTY_LABEL);

    const auto entry = ErrorEntry::from_tap(type, "Wrong Response", std::string("note"));
    REQUIRE(entry.has_value());
    CHECK(entry->m_ErrorType == type);
    CHECK(entry->m_ErrorTypeLabel == "Wrong Response");
    CHECK(entry->m_AttachedNote == std::optional<std::string>("note"));
}

TEST_CASE("Changing an entry's type takes a new snapshot and keeps the note", "[entry][snapshot][unit]")
{
    auto entry = ErrorEntry::from_tap(test_id<ErrorTypeId>(ERROR_WRONG_RESPONSE), "Wrong Response", std::string("note"));
    REQUIRE(entry.has_value());

    const auto other = test_id<ErrorTypeId>(ERROR_MUTE_DELAY);
    REQUIRE_FALSE(entry->retype(other, "").has_value());
    CHECK(entry->m_ErrorTypeLabel == "Wrong Response");  // a refused retype changes nothing

    REQUIRE(entry->retype(other, "Mute Delay").has_value());
    CHECK(entry->m_ErrorType == other);
    CHECK(entry->m_ErrorTypeLabel == "Mute Delay");
    CHECK(entry->m_AttachedNote == std::optional<std::string>("note"));

    auto scenario = blank_scenario();
    Entry stored;
    stored.m_Id = test_id<EntryId>(1);
    stored.m_Captured = on_day(10, 5);
    stored.m_Body = *entry;
    REQUIRE(scenario.m_Log.add(stored).has_value());
    rename_in_configuration(scenario);
    CHECK(render(scenario).ends_with("10:05 T| Mute Delay – note"));
}

TEST_CASE("Every golden file is unchanged when the entries carry snapshots equal to the names", "[export][snapshot][unit]")
{
    for (const auto name : golden_names())
    {
        auto scenario = make_scenario(name);
        const auto before = render_export(scenario.m_Log, scenario.m_Context);
        if (!before)
        {
            continue;
        }
        Log with_snapshots(scenario.m_Log.initial_header());
        for (Entry entry : scenario.m_Log.entries())
        {
            if (auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
            {
                error->m_ErrorTypeLabel = std::string(scenario.m_Context.m_ErrorTypeName(error->m_ErrorType));
            }
            REQUIRE(with_snapshots.add(std::move(entry)).has_value());
        }
        const auto after = render_export(with_snapshots, scenario.m_Context);
        REQUIRE(after.has_value());
        INFO(name);
        CHECK(*after == *before);
    }
}
