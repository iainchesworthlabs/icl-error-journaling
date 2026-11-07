// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.hpp"
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/export_context_builder.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::export_text;
using viewmodel::ExportContextBuilder;
using viewmodel::LogChange;
using Origin = LogChange::Origin;

namespace
{

ErrorTypeId type_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_ErrorTypes, name, &ErrorType::m_Name);
    REQUIRE(it != configuration.m_ErrorTypes.end());
    return it->m_Id;
}

LocationId location_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_Locations, name, &Location::m_Name);
    REQUIRE(it != configuration.m_Locations.end());
    return it->m_Id;
}

/// A source with the sample configuration in force.
struct Sample
{
    Sample()
    {
        m_Source.set(sample_configuration());
    }

    [[nodiscard]] const Configuration& configuration() const
    {
        return m_Source.configuration();
    }

    FakeConfigurationSource m_Source;
    FakeTimeZoneProvider m_Zones = make_fixed_zones();
};

/// The header of the golden scenario: the location is the configuration's North Site, by id.
HeaderValues prd_header(const Configuration& configuration)
{
    HeaderValues header = sample_header();
    header.m_Location = LocationValue{location_named(configuration, "North Site"), {}};
    return header;
}

Entry error_at(std::uint64_t number, int hour, int minute, ErrorTypeId type, std::string label,
               std::optional<std::string> note = std::nullopt)
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(number);
    entry.m_Captured = on_day(hour, minute);
    entry.m_Body = ErrorEntry{type, std::move(note), std::move(label)};
    return entry;
}

} // namespace

// ------------------------------------------------------------------------------------------------- the lookups

TEST_CASE("The context resolves live, retired and renamed types, locations and templates", "[viewmodel][export]")
{
    Sample sample;
    Configuration configuration = sample.configuration();
    const ErrorTypeId live = type_named(configuration, "Wrong Response");
    const ErrorTypeId retired = type_named(configuration, "Volume Jump");
    const ErrorTypeId renamed = type_named(configuration, "Mute Delay");
    for (ErrorType& type : configuration.m_ErrorTypes)
    {
        if (type.m_Id == retired)
        {
            type.m_Retired = true;
        }
        if (type.m_Id == renamed)
        {
            type.m_Name = "Mute Lag";
        }
    }
    configuration.m_Locations[1].m_Retired = true;
    sample.m_Source.set(configuration);

    const ExportContext context = ExportContextBuilder(sample.m_Source, sample.m_Zones).build();

    CHECK(context.m_ErrorTypeName(live) == "Wrong Response");
    CHECK(context.m_ErrorTypeName(retired) == "Volume Jump"); // retired types still resolve (a past log uses them)
    CHECK(context.m_ErrorTypeName(renamed) == "Mute Lag");    // the lookup gives the current name
    CHECK(context.m_LocationName(location_named(configuration, "North Site")) == "North Site");
    CHECK(context.m_LocationName(location_named(configuration, "South Site")) == "South Site"); // retired
    const NoteTemplate* played = context.m_Template(configuration.m_Templates[0].m_Id);
    REQUIRE(played != nullptr);
    CHECK(played->m_OutputPattern == "Played {value}");
    CHECK(context.m_Template(configuration.m_Templates[1].m_Id) != nullptr);
    CHECK(context.m_Zones == &sample.m_Zones);
}

TEST_CASE("An unknown id gives the empty answer the renderer reports as an error", "[viewmodel][export]")
{
    Sample sample;
    const ExportContext context = ExportContextBuilder(sample.m_Source, sample.m_Zones).build();
    const auto stranger = sample_id<ErrorTypeId>(9999);

    CHECK(context.m_ErrorTypeName(stranger).empty());
    CHECK(context.m_LocationName(sample_id<LocationId>(9999)).empty());
    CHECK(context.m_Template(sample_id<NoteTemplateId>(9999)) == nullptr);

    SECTION("an error type")
    {
        // An entry with no name snapshot (older data) is named by lookup, and the lookup fails.
        Log log(sample_header());
        REQUIRE(log.add(error_at(1, 10, 5, stranger, "")));
        const auto text = export_text(log, context);
        REQUIRE_FALSE(text.has_value());
        CHECK(text.error() == RenderError::UNKNOWN_ERROR_TYPE);
    }

    SECTION("a location")
    {
        HeaderValues header = sample_header();
        header.m_Location = LocationValue{sample_id<LocationId>(9999), {}};
        const Log log(header);
        const auto text = export_text(log, context);
        REQUIRE_FALSE(text.has_value());
        CHECK(text.error() == RenderError::UNKNOWN_LOCATION);
    }

    SECTION("a template")
    {
        Log log(sample_header());
        Entry entry;
        entry.m_Id = sample_id<EntryId>(1);
        entry.m_Captured = on_day(10, 5);
        entry.m_Body = NoteEntry{sample_id<NoteTemplateId>(9999), "Morning Medley"};
        REQUIRE(log.add(entry));
        const auto text = export_text(log, context);
        REQUIRE_FALSE(text.has_value());
        CHECK(text.error() == RenderError::UNKNOWN_TEMPLATE);
    }
}

TEST_CASE("The unit prefix and the time format come from the configuration in force", "[viewmodel][export]")
{
    Sample sample;
    Log log(prd_header(sample.configuration()));
    REQUIRE(log.add(error_at(1, 10, 5, type_named(sample.configuration(), "Wrong Response"), "Wrong Response")));

    const auto before = export_text(log, ExportContextBuilder(sample.m_Source, sample.m_Zones).build());
    REQUIRE(before.has_value());
    CHECK(before->find("UNIT #: UNIT-27") != std::string::npos);
    CHECK(before->find("\n10:05 T| Wrong Response") != std::string::npos);

    Configuration changed = sample.configuration();
    changed.m_UnitPrefix = "RACK";
    changed.m_TimeFormat.m_Style = TimeStyle::H12;
    sample.m_Source.set(changed);

    // A change of prefix or format applies to every log, past days included: neither is stored in a log.
    const auto after = export_text(log, ExportContextBuilder(sample.m_Source, sample.m_Zones).build());
    REQUIRE(after.has_value());
    CHECK(after->find("RACK #: RACK-27") != std::string::npos);
    CHECK(after->find("\n10:05 AM T| Wrong Response") != std::string::npos);
}

TEST_CASE("Building the context twice from an unchanged configuration renders identical text", "[viewmodel][export]")
{
    Sample sample;
    Configuration configuration = sample.configuration();
    configuration.m_IncludeTotals = true;
    sample.m_Source.set(configuration);
    Log log(prd_header(configuration));
    REQUIRE(log.add(error_at(1, 10, 5, type_named(configuration, "Wrong Response"), "Wrong Response")));
    REQUIRE(log.add(error_at(2, 10, 20, type_named(configuration, "Mute Delay"), "Mute Delay")));
    REQUIRE(log.add(error_at(3, 11, 2, type_named(configuration, "Wrong Response"), "Wrong Response")));
    const ExportContextBuilder builder(sample.m_Source, sample.m_Zones);

    const auto first = export_text(log, builder.build());
    const auto second = export_text(log, builder.build());

    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == *second);
    CHECK(first->find("\n\nTotals: ") != std::string::npos);
}

TEST_CASE("The totals follow board order, then the master list, retired types included", "[viewmodel][export]")
{
    Sample sample;
    Configuration configuration = sample.configuration();
    configuration.m_IncludeTotals = true;
    // Retire the type on board position 1 (it keeps its position but leaves the layout) and add one off the board.
    configuration.m_ErrorTypes.front().m_Retired = true;
    ErrorType extra;
    extra.m_Id = sample_id<ErrorTypeId>(500);
    extra.m_Name = "Wrong Greeting";
    configuration.m_ErrorTypes.push_back(extra);
    sample.m_Source.set(configuration);

    const ExportContext context = ExportContextBuilder(sample.m_Source, sample.m_Zones).build();

    REQUIRE(context.m_TotalsOrder.size() == configuration.m_ErrorTypes.size());
    CHECK(context.m_TotalsOrder.front() == configuration.m_ErrorTypes[1].m_Id); // board position 2 leads
    CHECK(context.m_TotalsOrder[configuration.m_ErrorTypes.size() - 2] == configuration.m_ErrorTypes[0].m_Id);
    CHECK(context.m_TotalsOrder.back() == extra.m_Id); // the master list's order for the rest
}

TEST_CASE("Without a configuration the context is empty and nothing resolves", "[viewmodel][export]")
{
    FakeConfigurationSource unconfigured;
    FakeTimeZoneProvider zones = make_fixed_zones();

    const ExportContext context = ExportContextBuilder(unconfigured, zones).build();

    CHECK(context.m_UnitPrefix.empty());
    CHECK_FALSE(context.m_IncludeTotals);
    CHECK(context.m_TotalsOrder.empty());
    CHECK(context.m_ErrorTypeName(sample_id<ErrorTypeId>(1)).empty());
}

// ------------------------------------------------------------------------------------------------------- the text

TEST_CASE("export_text of a log built by commands is the PRD example, byte for byte", "[viewmodel][export][golden]")
{
    DocumentRig rig(false);
    Sample sample;
    const Configuration& configuration = sample.configuration();

    // The day of the PRD example, made the way the services will make it: a document and its commands.
    // The rig's log is not the day's: starting the day's archives it.
    const LogId day = sample_id<LogId>(2);
    REQUIRE(rig.storage().memory().start_new_log(rig.log_id(), day, prd_header(configuration)));
    rig.document = std::make_shared<viewmodel::LogDocument>(rig.world().environment(), rig.gateway(),
                                                            rig.storage().memory().load_log(day).value());

    const ErrorTypeId wrong_response = type_named(configuration, "Wrong Response");
    const ErrorTypeId mute_delay = type_named(configuration, "Mute Delay");
    const ErrorTypeId wrong_track = type_named(configuration, "Wrong Track Played");
    const ErrorTypeId speech_delay = type_named(configuration, "Speech Delay");
    Entry note;
    note.m_Id = sample_id<EntryId>(3);
    note.m_Captured = on_day(10, 30);
    note.m_Body = NoteEntry{std::nullopt, "Played Morning Medley"};

    REQUIRE(rig.document->add_entry(error_at(1, 10, 5, wrong_response, "Wrong Response"), Origin::USER, {}));
    REQUIRE(rig.document->add_entry(error_at(2, 10, 20, mute_delay, "Mute Delay"), Origin::USER, {}));
    REQUIRE(rig.document->add_entry(std::move(note), Origin::USER, {}));
    REQUIRE(rig.document->add_entry(error_at(4, 11, 2, wrong_track, "Wrong Track Played", "asked for Evening Song"),
                                    Origin::USER, {}));
    REQUIRE(
        rig.document->change_header(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(13, 15), sample_id<EntryId>(5), {}));
    REQUIRE(rig.document->add_entry(error_at(6, 13, 40, speech_delay, "Speech Delay"), Origin::USER, {}));
    rig.gateway().run_all();

    const ExportContext context = ExportContextBuilder(sample.m_Source, sample.m_Zones).build();
    const auto text = export_text(rig.document->log(), context);

    REQUIRE(text.has_value());
    check_golden("prd-example", *text);

    // The store has the same log, so a log opened for review later reads the same.
    const auto stored = rig.storage().memory().load_log(day);
    REQUIRE(stored.has_value());
    const auto from_store = export_text(stored->m_Log, context);
    REQUIRE(from_store.has_value());
    CHECK(*from_store == *text);
}

TEST_CASE("export_text is render_export: one function for Copy, Share and the golden test", "[viewmodel][export]")
{
    const auto scenario = make_scenario("prd-example");

    const auto text = export_text(scenario.m_Log, scenario.m_Context);
    const auto direct = render_export(scenario.m_Log, scenario.m_Context);

    REQUIRE(text.has_value());
    REQUIRE(direct.has_value());
    CHECK(*text == *direct);
    check_golden("prd-example", *text);
}
