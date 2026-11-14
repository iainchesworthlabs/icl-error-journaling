// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden_scenarios.hpp"

#include <chrono>
#include <cstdlib>
#include <functional>
#include <map>
#include <tuple>
#include <string>
#include <utility>

#include "error_journaling/header_history.hpp"
#include "fake_time_zone_provider.hpp"

namespace ej::test
{

namespace
{

using namespace std::chrono_literals;

/// What a configuration would supply: names, templates and the zone provider, all fictional.
struct World
{
    std::unique_ptr<TimeZoneProvider> m_Zones;
    std::map<ErrorTypeId, std::string> m_ErrorNames;
    std::map<LocationId, std::string> m_LocationNames;
    std::map<NoteTemplateId, NoteTemplate> m_Templates;
    std::vector<ErrorTypeId> m_TotalsOrder;
    std::map<InteractionTypeId, std::string> m_InteractionNames;
    std::map<InteractionTypeId, std::string> m_InteractionPatterns;
    std::map<PersonId, std::string> m_People;  // the fictional list of AGENTS.md; a forgotten person is not here
};

std::unique_ptr<World> make_world(bool real_zones)
{
    auto world = std::make_unique<World>();
    if (real_zones)
    {
        world->m_Zones = make_qt_time_zone_provider();
    }
    else
    {
        world->m_Zones = std::make_unique<FakeTimeZoneProvider>(
            std::map<std::string, std::chrono::minutes, std::less<>>{
                {"Australia/Melbourne", 11h}, {"Australia/Perth", 8h}, {"UTC", 0h}});
    }
    const std::pair<std::uint64_t, const char*> errors[] = {
        {ERROR_WRONG_RESPONSE, "Wrong Response"}, {ERROR_MUTE_DELAY, "Mute Delay"},
        {ERROR_WRONG_TRACK_PLAYED, "Wrong Track Played"}, {ERROR_SPEECH_DELAY, "Speech Delay"},
        {ERROR_LATE_GREETING, "Late Greeting"}, {ERROR_ECHO_DELAY_RETIRED, "Echo Delay"}};
    for (const auto& [number, name] : errors)
    {
        world->m_ErrorNames[test_id<ErrorTypeId>(number)] = name;
        world->m_TotalsOrder.push_back(test_id<ErrorTypeId>(number));
    }
    world->m_LocationNames[test_id<LocationId>(LOCATION_NORTH_SITE)] = "North Site";
    world->m_LocationNames[test_id<LocationId>(LOCATION_SOUTH_SITE)] = "South Site";

    const std::tuple<std::uint64_t, const char*, const char*> templates[] = {
        {TEMPLATE_PLAYED, "Played song", "Played {value}"},
        {TEMPLATE_VISITOR, "Visitor", "Visitor interaction: {value}"},
        {TEMPLATE_REQUESTED, "Requested song", "Song {value} requested twice"}};
    for (const auto& [number, name, pattern] : templates)
    {
        NoteTemplate t;
        t.m_Id = test_id<NoteTemplateId>(number);
        t.m_Name = name;
        t.m_ValuePrompt = "Which one?";
        t.m_OutputPattern = pattern;
        world->m_Templates[t.m_Id] = t;
    }
    const std::tuple<std::uint64_t, const char*, const char*> interactions[] = {
        {INTERACTION_GREETED, "Greeted", ""},
        {INTERACTION_SPOKE_WITH, "Spoke with", "Spoke with {person}"},
        {INTERACTION_PLAYED_SONG, "Played song", "Played {value}"},
        {INTERACTION_VISITOR, "Visitor interaction", ""},
        {INTERACTION_GAVE, "Gave", "Gave {person} {value}"}};
    for (const auto& [number, name, pattern] : interactions)
    {
        const auto id = test_id<InteractionTypeId>(number);
        world->m_InteractionNames[id] = name;
        if (*pattern != 0)
        {
            world->m_InteractionPatterns[id] = pattern;
        }
    }
    world->m_People[test_id<PersonId>(PERSON_ALEX)] = "Alex Sample";
    world->m_People[test_id<PersonId>(PERSON_JORDAN)] = "Jordan Placeholder";
    world->m_People[test_id<PersonId>(PERSON_PAT)] = "Pat Stand-in";
    return world;
}

ExportContext make_context(World& world)
{
    ExportContext context;
    context.m_UnitPrefix = "UNIT";
    context.m_Zones = world.m_Zones.get();
    context.m_TotalsOrder = world.m_TotalsOrder;
    context.m_ErrorTypeName = [&world](ErrorTypeId id) -> std::string_view
    {
        const auto it = world.m_ErrorNames.find(id);
        return it == world.m_ErrorNames.end() ? std::string_view{} : std::string_view(it->second);
    };
    context.m_LocationName = [&world](LocationId id) -> std::string_view
    {
        const auto it = world.m_LocationNames.find(id);
        return it == world.m_LocationNames.end() ? std::string_view{} : std::string_view(it->second);
    };
    context.m_Template = [&world](NoteTemplateId id) -> const NoteTemplate*
    {
        const auto it = world.m_Templates.find(id);
        return it == world.m_Templates.end() ? nullptr : &it->second;
    };
    context.m_InteractionTypeName = [&world](InteractionTypeId id) -> std::string_view
    {
        const auto it = world.m_InteractionNames.find(id);
        return it == world.m_InteractionNames.end() ? std::string_view{} : std::string_view(it->second);
    };
    context.m_InteractionPattern = [&world](InteractionTypeId id) -> std::optional<std::string>
    {
        const auto it = world.m_InteractionPatterns.find(id);
        return it == world.m_InteractionPatterns.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    context.m_PersonName = [&world](const PersonId& id) -> std::optional<std::string>
    {
        const auto it = world.m_People.find(id);
        return it == world.m_People.end() ? std::nullopt : std::optional<std::string>(it->second);
    };
    return context;
}

HeaderValues standard_header()
{
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    header.m_UnitNumber = UnitNumber::parse("27").value();
    header.m_Location = LocationValue{test_id<LocationId>(LOCATION_NORTH_SITE), {}};
    header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2").value();
    return header;
}

HeaderValues empty_header()
{
    HeaderValues header = standard_header();
    header.m_UnitNumber.reset();
    header.m_Location.reset();
    header.m_SoftwareVersion.reset();
    return header;
}

std::chrono::minutes offset_of(std::string_view zone)
{
    if (zone == "Australia/Perth")
    {
        return 8h;
    }
    if (zone == "UTC")
    {
        return 0h;
    }
    return 11h;  // Australia/Melbourne on 7 Nov 2026
}

/// A scenario under construction. Entry ids count up from 1, so a scenario's ids are stable.
class Builder
{
public:
    explicit Builder(HeaderValues header = standard_header(), bool real_zones = false)
        : m_World(make_world(real_zones)), m_Log(std::move(header))
    {
    }

    Log& log() { return m_Log; }
    World& world() { return *m_World; }
    std::uint64_t next_id() { return m_NextId++; }
    bool totals = false;
    TimeFormat time_format;
    std::string_view prefix = "UNIT";
    std::string_view marker = export_format::DEFAULT_INTERACTION_MARKER;
    NameLevel level = NameLevel::INITIALS;

    void add(Entry entry)
    {
        if (!m_Log.add(std::move(entry)))
        {
            std::abort();  // a duplicate id is a bug in the test data
        }
    }
    void error(Timestamp when, std::uint64_t type, std::optional<std::string> note = std::nullopt)
    {
        add(error_entry(next_id(), std::move(when), type, std::move(note)));
    }
    void interaction(Timestamp when, std::uint64_t type, std::vector<std::uint64_t> people = {},
                     std::optional<std::string> value = std::nullopt, std::optional<std::string> note = std::nullopt)
    {
        add(interaction_entry(next_id(), std::move(when), type, std::move(people), std::move(value), std::move(note)));
    }
    void note(Timestamp when, std::string text) { add(free_note_entry(next_id(), std::move(when), std::move(text))); }
    void templated(Timestamp when, std::uint64_t tpl, std::string value)
    {
        add(template_note_entry(next_id(), std::move(when), tpl, std::move(value)));
    }
    /// Through change_header, so the stored text is validated and canonical.
    EntryId change(HeaderField field, std::string value, Timestamp when)
    {
        const EntryId id = test_id<EntryId>(next_id());
        if (!change_header(m_Log, field, std::move(value), std::move(when), id))
        {
            std::abort();
        }
        return id;
    }

    Scenario finish()
    {
        ExportContext context = make_context(*m_World);
        context.m_UnitPrefix = prefix;
        context.m_TimeFormat = time_format;
        context.m_IncludeTotals = totals;
        context.m_InteractionMarker = marker;
        context.m_NameLevel = level;
        return Scenario{std::move(m_Log), std::move(context), std::shared_ptr<void>(std::move(m_World))};
    }

private:
    std::unique_ptr<World> m_World;
    Log m_Log;
    std::uint64_t m_NextId = 1;
};

std::string location_text(std::uint64_t number)
{
    return test_id<LocationId>(number).format();
}

/// A UTC instant, with the zone it is captured in.
Timestamp utc_at(std::string_view zone, int y, unsigned m, unsigned d, int hh, int mm, int ss = 0)
{
    return Timestamp{utc(y, m, d, hh, mm, ss), std::string(zone)};
}

/// Used by the totals and deleted-entry scenarios: a busy day with a type used three times, once, and a retired type.
void busy_day(Builder& b)
{
    b.error(on_day(9, 0), ERROR_WRONG_RESPONSE);
    b.error(on_day(9, 10), ERROR_MUTE_DELAY);
    b.error(on_day(9, 20), ERROR_WRONG_RESPONSE);
    b.templated(on_day(9, 30), TEMPLATE_PLAYED, "Morning Medley");
    b.error(on_day(9, 45), ERROR_WRONG_RESPONSE);
    b.error(on_day(10, 0), ERROR_WRONG_TRACK_PLAYED, "asked for Evening Song");
    b.error(on_day(10, 15), ERROR_ECHO_DELAY_RETIRED);
    b.error(on_day(10, 30), ERROR_WRONG_TRACK_PLAYED);
    b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(10, 40));
}

/// The log of the interaction scenarios: errors, notes and interactions interleaved, one interaction deleted, one with
/// a person who was forgotten, one with several people. The scenarios differ only in the label and the name level.
void interaction_day(Builder& b)
{
    b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);                                       // 1
    b.interaction(on_day(10, 12), INTERACTION_SPOKE_WITH, {PERSON_ALEX});               // 2
    b.error(on_day(10, 20), ERROR_MUTE_DELAY);                                          // 3
    b.interaction(on_day(10, 31), INTERACTION_PLAYED_SONG, {}, "Morning Medley");       // 4
    b.interaction(on_day(10, 40), INTERACTION_SPOKE_WITH, {PERSON_ALEX, PERSON_JORDAN, PERSON_PAT});  // 5
    b.note(on_day(10, 45), "Robot restarted");                                          // 6
    b.interaction(on_day(10, 48), INTERACTION_VISITOR, {}, std::nullopt, "asked about the schedule");  // 7
    b.interaction(on_day(11, 0), INTERACTION_GAVE, {PERSON_JORDAN}, "a card");          // 8
    b.interaction(on_day(11, 10), INTERACTION_SPOKE_WITH, {PERSON_FORGOTTEN});          // 9
    b.interaction(on_day(11, 20), INTERACTION_SPOKE_WITH);                              // 10: nobody picked yet
    b.interaction(on_day(11, 30), INTERACTION_GREETED);                                 // 11: deleted
    b.error(on_day(11, 40), ERROR_WRONG_RESPONSE);                                      // 12
    if (!b.log().set_deleted(test_id<EntryId>(11), true))
    {
        std::abort();
    }
}

using Factory = std::function<Scenario()>;

const std::vector<std::pair<std::string_view, Factory>>& factories()
{
    static const std::vector<std::pair<std::string_view, Factory>> all = {
        {"prd-example",
         []
         {
             Builder b;
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             b.error(on_day(10, 20), ERROR_MUTE_DELAY);
             b.templated(on_day(10, 30), TEMPLATE_PLAYED, "Morning Medley");
             b.error(on_day(11, 2), ERROR_WRONG_TRACK_PLAYED, "asked for Evening Song");
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(13, 15));
             b.error(on_day(13, 40), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"empty-log", [] { return Builder().finish(); }},
        {"interactions-initials",
         []
         {
             Builder b;
             b.totals = true;  // interactions are not counted
             interaction_day(b);
             return b.finish();
         }},
        {"interactions-full",
         []
         {
             Builder b;
             b.level = NameLevel::FULL;
             interaction_day(b);
             return b.finish();
         }},
        {"interactions-hidden",
         []
         {
             Builder b;
             b.level = NameLevel::HIDDEN;
             interaction_day(b);
             return b.finish();
         }},
        {"interactions-no-label",
         []
         {
             Builder b;
             b.marker = "";
             interaction_day(b);
             return b.finish();
         }},
        {"empty-header",
         []
         {
             Builder b(empty_header());
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             b.note(on_day(10, 15), "Robot restarted");
             b.error(on_day(10, 25), ERROR_MUTE_DELAY);
             return b.finish();
         }},
        {"partial-header",
         []
         {
             HeaderValues header = standard_header();
             header.m_Location.reset();
             Builder b(header);
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             b.change(HeaderField::LOCATION, "Back Room Annex", on_day(10, 30));
             b.error(on_day(10, 45), ERROR_MUTE_DELAY);
             return b.finish();
         }},
        {"attached-note",
         []
         {
             Builder b;
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE, "asked for Evening Song");
             b.error(on_day(10, 20), ERROR_MUTE_DELAY, "caller said wait");
             b.note(on_day(10, 20, 30), "Restarted the robot");
             b.error(on_day(10, 40), ERROR_WRONG_TRACK_PLAYED, "went from side A \xE2\x86\x92 side B \xE2\x80\x93 twice");
             return b.finish();
         }},
        {"templated-notes",
         []
         {
             Builder b;
             b.templated(on_day(9, 0), TEMPLATE_PLAYED, "Morning Medley");
             b.templated(on_day(9, 30), TEMPLATE_VISITOR, "Sam Example waved");
             b.templated(on_day(10, 0), TEMPLATE_REQUESTED, "Evening Song");
             b.templated(on_day(10, 15), TEMPLATE_PLAYED, "  Spaced Song  ");
             b.note(on_day(10, 30), "Calibration done");
             return b.finish();
         }},
        {"version-change-midday",
         []
         {
             Builder b;
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(11, 0));
             b.error(on_day(11, 30), ERROR_MUTE_DELAY);
             b.change(HeaderField::UNIT_NUMBER, "31", on_day(12, 0));
             b.change(HeaderField::LOCATION, location_text(LOCATION_SOUTH_SITE), on_day(13, 0));
             b.error(on_day(13, 30), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"header-change-corrected",
         []
         {
             Builder b;
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             // A change then a correction: the line reads 1.4.2 -> 1.5.1 and the header block keeps 1.4.2.
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(11, 0));
             if (!correct_header(b.log(), HeaderField::SOFTWARE_VERSION, "1.5.1"))
             {
                 std::abort();
             }
             // A correction then a change: the unit number starts as 28 after the fix, the line reads 28 -> 31.
             if (!correct_header(b.log(), HeaderField::UNIT_NUMBER, "28"))
             {
                 std::abort();
             }
             b.change(HeaderField::UNIT_NUMBER, "31", on_day(12, 0));
             b.error(on_day(12, 30), ERROR_MUTE_DELAY);
             return b.finish();
         }},
        {"header-change-from-empty",
         []
         {
             HeaderValues header = standard_header();
             header.m_Location.reset();
             header.m_SoftwareVersion.reset();
             Builder b(header);
             b.change(HeaderField::SOFTWARE_VERSION, "1.4.2", on_day(10, 0));
             b.change(HeaderField::LOCATION, location_text(LOCATION_NORTH_SITE), on_day(10, 5));
             b.error(on_day(10, 30), ERROR_WRONG_RESPONSE);
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(12, 0));
             return b.finish();
         }},
        {"totals-off",
         []
         {
             Builder b;
             busy_day(b);
             return b.finish();
         }},
        {"totals-on",
         []
         {
             Builder b;
             b.totals = true;
             busy_day(b);
             return b.finish();
         }},
        {"totals-on-empty",
         []
         {
             Builder b;
             b.totals = true;
             b.note(on_day(10, 0), "Setup only, no errors yet");
             return b.finish();
         }},
        {"deleted-entries",
         []
         {
             Builder b;
             b.totals = true;
             b.error(on_day(10, 0), ERROR_WRONG_RESPONSE);
             b.error(on_day(10, 10), ERROR_MUTE_DELAY);  // 2: deleted
             b.note(on_day(10, 20), "Robot restarted");
             b.note(on_day(10, 30), "Deleted note");  // 4: deleted
             const EntryId first_change = b.change(HeaderField::SOFTWARE_VERSION, "1.4.9", on_day(11, 0));
             const EntryId second_change = b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(12, 0));
             b.error(on_day(13, 0), ERROR_WRONG_RESPONSE);
             for (const auto id : {test_id<EntryId>(2), test_id<EntryId>(4), first_change})
             {
                 if (!b.log().set_deleted(id, true))
                 {
                     std::abort();
                 }
             }
             (void)second_change;  // stays live: its old side is 1.4.2 because the change before it is deleted
             return b.finish();
         }},
        {"twelve-hour",
         []
         {
             Builder b;
             b.time_format = TimeFormat{TimeStyle::H12, false, MeridiemCase::UPPER};
             b.error(on_day(0, 0), ERROR_WRONG_RESPONSE);
             b.error(on_day(0, 5), ERROR_MUTE_DELAY);
             b.error(on_day(9, 7), ERROR_WRONG_RESPONSE);
             b.error(on_day(12, 0), ERROR_WRONG_TRACK_PLAYED);
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(13, 15));
             b.note(on_day(13, 20), "Back after lunch");
             b.error(on_day(23, 59), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"twelve-hour-lower",
         []
         {
             Builder b;
             b.time_format = TimeFormat{TimeStyle::H12, false, MeridiemCase::LOWER};
             b.error(on_day(0, 0), ERROR_WRONG_RESPONSE);
             b.error(on_day(9, 7), ERROR_MUTE_DELAY);
             b.error(on_day(12, 0), ERROR_WRONG_TRACK_PLAYED);
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(13, 15));
             b.error(on_day(23, 59), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"with-seconds",
         []
         {
             Builder b;
             b.time_format = TimeFormat{TimeStyle::H24, true, MeridiemCase::UPPER};
             b.error(on_day(0, 0, 0), ERROR_WRONG_RESPONSE);
             b.error(on_day(9, 7, 3), ERROR_MUTE_DELAY);
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(13, 15, 59));
             b.error(on_day(23, 59, 59, 999), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"with-seconds-twelve-hour",
         []
         {
             Builder b;
             b.time_format = TimeFormat{TimeStyle::H12, true, MeridiemCase::UPPER};
             b.error(on_day(0, 0, 0), ERROR_WRONG_RESPONSE);
             b.error(on_day(9, 7, 3), ERROR_MUTE_DELAY);
             b.error(on_day(13, 15, 59), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"with-seconds-twelve-hour-lower",
         []
         {
             Builder b;
             b.time_format = TimeFormat{TimeStyle::H12, true, MeridiemCase::LOWER};
             b.error(on_day(0, 0, 0), ERROR_WRONG_RESPONSE);
             b.error(on_day(9, 7, 3), ERROR_MUTE_DELAY);
             b.error(on_day(13, 15, 59), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"custom-prefix",
         []
         {
             HeaderValues header = standard_header();
             header.m_UnitNumber = UnitNumber::parse("9").value();
             Builder b(header);
             b.prefix = "DEMO";
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             b.change(HeaderField::UNIT_NUMBER, "12", on_day(11, 0));
             b.error(on_day(11, 30), ERROR_MUTE_DELAY);
             return b.finish();
         }},
        {"unit-environment",
         []
         {
             HeaderValues header = standard_header();
             header.m_UnitNumber = UnitNumber::parse("27", "prod").value();
             Builder b(header);
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             // The environment changes, the number stays; then the unit leaves the environment and takes a new number.
             b.change(HeaderField::UNIT_NUMBER, "27-dev", on_day(11, 0));
             b.error(on_day(11, 30), ERROR_MUTE_DELAY);
             b.change(HeaderField::UNIT_NUMBER, "31", on_day(12, 0));
             b.error(on_day(12, 30), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"version-commit",
         []
         {
             HeaderValues header = standard_header();
             header.m_SoftwareVersion =
                 SoftwareVersion::parse("1.4.2", "A1B2C3D4E5F6A7B8C9D0A1B2C3D4E5F6A7B8C9D0").value();
             Builder b(header);
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             // A new version with its own commit, the same version without one, then the commit alone.
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0+e4f5a6b", on_day(11, 0));
             b.error(on_day(11, 30), ERROR_MUTE_DELAY);
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0", on_day(12, 0));
             b.change(HeaderField::SOFTWARE_VERSION, "1.5.0+0123456789ab", on_day(12, 15));
             b.error(on_day(12, 30), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"same-minute-order",
         []
         {
             Builder b;
             // Added out of time order. Sorted by UTC; the same millisecond keeps insertion order.
             b.error(on_day(10, 5, 40, 500), ERROR_WRONG_RESPONSE);
             b.error(on_day(10, 5, 10), ERROR_MUTE_DELAY);
             b.error(on_day(10, 5, 40, 500), ERROR_WRONG_TRACK_PLAYED);
             b.note(on_day(10, 5, 40, 500), "Same millisecond note");
             b.error(on_day(10, 5, 10), ERROR_SPEECH_DELAY);
             return b.finish();
         }},
        {"past-midnight",
         []
         {
             Builder b;
             b.error(on_day(23, 50), ERROR_WRONG_RESPONSE);
             b.error(Timestamp{on_day(0, 10).m_Utc + 24h, "Australia/Melbourne"}, ERROR_MUTE_DELAY);
             return b.finish();
         }},
        {"melbourne-dst-spring",
         []
         {
             HeaderValues header = standard_header();
             header.m_LocalDate = std::chrono::year{2026} / 10 / 4;
             Builder b(header, true);
             // 2026-10-04 02:00 AEST becomes 03:00 AEDT. Instants are UTC on 2026-10-03.
             b.error(utc_at("Australia/Melbourne", 2026, 10, 3, 15, 50), ERROR_WRONG_RESPONSE);  // 01:50 AEST
             // An entry edited into the missing hour: 02:30 does not exist, so the provider resolves it.
             const auto resolved = b.world().m_Zones->to_utc(local(2026, 10, 4, 2, 30), "Australia/Melbourne");
             b.error(Timestamp{resolved.value(), "Australia/Melbourne"}, ERROR_MUTE_DELAY);
             b.error(utc_at("Australia/Melbourne", 2026, 10, 3, 16, 5), ERROR_SPEECH_DELAY);  // 03:05 AEDT
             return b.finish();
         }},
        {"melbourne-dst-autumn",
         []
         {
             HeaderValues header = standard_header();
             header.m_LocalDate = std::chrono::year{2026} / 4 / 5;
             Builder b(header, true);
             // 2026-04-05 03:00 AEDT becomes 02:00 AEST. Instants are UTC on 2026-04-04.
             b.error(utc_at("Australia/Melbourne", 2026, 4, 4, 14, 50), ERROR_WRONG_RESPONSE);  // 01:50 AEDT
             b.error(utc_at("Australia/Melbourne", 2026, 4, 4, 15, 30), ERROR_MUTE_DELAY);      // 02:30 AEDT
             b.error(utc_at("Australia/Melbourne", 2026, 4, 4, 16, 30), ERROR_SPEECH_DELAY);    // 02:30 AEST
             b.error(utc_at("Australia/Melbourne", 2026, 4, 4, 17, 10), ERROR_WRONG_TRACK_PLAYED);  // 03:10 AEST
             return b.finish();
         }},
        {"mixed-zones",
         []
         {
             Builder b;
             b.error(on_day(10, 5), ERROR_WRONG_RESPONSE);
             b.error(on_day(8, 10, 0, 0, "Australia/Perth"), ERROR_MUTE_DELAY);
             b.error(on_day(0, 20, 0, 0, "UTC"), ERROR_WRONG_TRACK_PLAYED);
             b.note(on_day(12, 0), "Back in Melbourne");
             return b.finish();
         }},
        {"multiline-and-whitespace",
         []
         {
             Builder b;
             b.note(on_day(10, 0), "Line one\nline two\n\nline four");
             b.note(on_day(10, 5), "tab\tseparated\tvalues");
             b.note(on_day(10, 10), "double  spaces   inside");
             b.note(on_day(10, 15), "trailing spaces   ");
             b.note(on_day(10, 20), "  leading spaces and\r\nCRLF  ");
             b.note(on_day(10, 25), "   ");
             b.templated(on_day(10, 30), TEMPLATE_PLAYED, "Two\nLines");
             return b.finish();
         }},
        {"retired-type",
         []
         {
             Builder b;
             b.error(on_day(10, 5), ERROR_ECHO_DELAY_RETIRED);
             b.error(on_day(10, 20), ERROR_WRONG_RESPONSE, "was retired later");
             return b.finish();
         }},
    };
    return all;
}

}  // namespace

Timestamp on_day(int hour, int minute, int second, int millisecond, std::string_view zone)
{
    const auto local_midnight = std::chrono::sys_days{std::chrono::year{2026} / 11 / 7} - offset_of(zone);
    return Timestamp{UtcMillis{std::chrono::duration_cast<std::chrono::milliseconds>(local_midnight.time_since_epoch())
                               + std::chrono::hours(hour) + std::chrono::minutes(minute)
                               + std::chrono::seconds(second) + std::chrono::milliseconds(millisecond)},
                     std::string(zone)};
}

Entry error_entry(std::uint64_t id, Timestamp when, std::uint64_t error_type, std::optional<std::string> attached_note)
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = std::move(when);
    entry.m_Body = ErrorEntry{test_id<ErrorTypeId>(error_type), std::move(attached_note)};
    return entry;
}

Entry free_note_entry(std::uint64_t id, Timestamp when, std::string text)
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = std::move(when);
    entry.m_Body = NoteEntry{std::nullopt, std::move(text)};
    return entry;
}

Entry interaction_entry(std::uint64_t id, Timestamp when, std::uint64_t type, std::vector<std::uint64_t> people,
                        std::optional<std::string> value, std::optional<std::string> attached_note)
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = std::move(when);
    InteractionEntry body;
    body.m_Type = test_id<InteractionTypeId>(type);
    // The snapshot of the type's name, as the app writes it at the tap.
    switch (type)
    {
    case INTERACTION_GREETED: body.m_TypeLabel = "Greeted"; break;
    case INTERACTION_SPOKE_WITH: body.m_TypeLabel = "Spoke with"; break;
    case INTERACTION_PLAYED_SONG: body.m_TypeLabel = "Played song"; break;
    case INTERACTION_VISITOR: body.m_TypeLabel = "Visitor interaction"; break;
    default: body.m_TypeLabel = "Gave"; break;
    }
    for (const auto person : people)
    {
        body.m_People.push_back(test_id<PersonId>(person));
    }
    body.m_Value = std::move(value);
    body.m_AttachedNote = std::move(attached_note);
    entry.m_Body = std::move(body);
    return entry;
}

Entry template_note_entry(std::uint64_t id, Timestamp when, std::uint64_t template_number, std::string value)
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = std::move(when);
    entry.m_Body = NoteEntry{test_id<NoteTemplateId>(template_number), std::move(value)};
    return entry;
}

const std::vector<std::string_view>& golden_names()
{
    static const std::vector<std::string_view> names = []
    {
        std::vector<std::string_view> result;
        for (const auto& [name, factory] : factories())
        {
            result.push_back(name);
        }
        return result;
    }();
    return names;
}

Scenario make_scenario(std::string_view name)
{
    for (const auto& [candidate, factory] : factories())
    {
        if (candidate == name)
        {
            return factory();
        }
    }
    std::abort();  // an unknown scenario name is a bug in the test
}

Scenario blank_scenario()
{
    return Builder().finish();
}

}  // namespace ej::test
