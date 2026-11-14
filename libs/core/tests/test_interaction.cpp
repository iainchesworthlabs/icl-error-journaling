// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/interaction.hpp"
#include "error_journaling/journal_record.hpp"
#include "error_journaling/person.hpp"
#include "error_journaling/stored_log.hpp"
#include "fake_time_zone_provider.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

// The saved people of the tests are the fictional names of AGENTS.md.
constexpr const char* ALEX = "Alex Sample";
constexpr const char* JORDAN = "Jordan Placeholder";
constexpr const char* PAT = "Pat Stand-in";

/// `,"crc":"xxxxxxxx"}`
constexpr std::size_t CRC_SUFFIX_BYTES = 18;

using Names = std::vector<std::optional<std::string>>;

std::string people_at(const Names& names, NameLevel level)
{
    return render_people(names, level);
}

std::string with_crc(const std::string& object)
{
    char digits[16];
    std::snprintf(digits, sizeof digits, "%08x", crc32(object));
    return object.substr(0, object.size() - 1) + ",\"crc\":\"" + digits + "\"}";
}

Entry sample_interaction(std::uint64_t id, std::int64_t minute)
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(id);
    entry.m_Captured = Timestamp{sample_time(minute), "Europe/London"};
    InteractionEntry body;
    body.m_Type = sample_id<InteractionTypeId>(3);
    body.m_TypeLabel = "Spoke with";
    body.m_People = {sample_id<PersonId>(1), sample_id<PersonId>(2)};
    body.m_Value = "Morning Medley";
    body.m_AttachedNote = "asked about \"the schedule\"";
    entry.m_Body = std::move(body);
    return entry;
}

JournalRecord interaction_record(std::uint64_t seq, Entry entry)
{
    JournalBody body = AddEntryBody{std::move(entry)};
    return JournalRecord{journal_version_for(body), seq, sample_id<LogId>(100), std::move(body)};
}

}  // namespace

TEST_CASE("interaction.name_rules: a name is trimmed, collapsed and bounded", "[interaction][name]")
{
    CHECK(normalise_person_name("  Alex   Sample \t").value() == "Alex Sample");
    CHECK(normalise_person_name("Alex\n\nSample").value() == "Alex Sample");
    CHECK(normalise_person_name("Alex\xC2\xA0Sample").value() == "Alex Sample"); // no-break space
    CHECK(normalise_person_name("Alex Sample").value() == "Alex Sample");

    CHECK(normalise_person_name("").error() == NameError::EMPTY);
    CHECK(normalise_person_name("   \t ").error() == NameError::EMPTY);
    CHECK(normalise_person_name("broken \xC3").error() == NameError::NOT_UTF8);

    // 60 code points are allowed, 61 are not; the count is of code points, not bytes.
    CHECK(normalise_person_name(std::string(60, 'a')).has_value());
    CHECK(normalise_person_name(std::string(61, 'a')).error() == NameError::TOO_LONG);
    std::string wide;
    for (int i = 0; i < 60; ++i)
    {
        wide += "\xE6\x9D\x8E"; // three bytes each
    }
    CHECK(normalise_person_name(wide).has_value());
    CHECK(normalise_person_name(wide + "\xE6\x9D\x8E").error() == NameError::TOO_LONG);
    // The length is checked after collapsing.
    CHECK(normalise_person_name(std::string(30, 'a') + std::string(40, ' ') + std::string(29, 'b')).has_value());
}

TEST_CASE("interaction.name_rules: names match without regard to case, spacing or accents", "[interaction][name]")
{
    CHECK(person_names_match("alex sample", "Alex Sample"));
    CHECK(person_names_match("  ALEX   sample ", "Alex Sample"));
    CHECK(person_names_match("Jos\xC3\xA9", "Jose"));
    CHECK(person_names_match("Jose\xCC\x81", "Jos\xC3\xA9")); // composed and decomposed forms
    CHECK(person_names_match("\xC3\x89mile", "emile"));
    CHECK_FALSE(person_names_match("Alex Sample", "Alex Samples"));
    CHECK_FALSE(person_names_match("Alex Sample", "Jordan Placeholder"));
    // A name that does not normalise matches nothing, not even itself.
    CHECK_FALSE(person_names_match("", ""));
    CHECK(person_name_key("Alex Sample") == person_name_key("alex sample"));
}

TEST_CASE("interaction.line_render: a type with no pattern reads as its name", "[interaction][line]")
{
    CHECK(render_interaction_text(std::nullopt, "Visitor interaction", "someone", std::nullopt) ==
          "Visitor interaction");
}

TEST_CASE("interaction.line_render: one slot, both slots and none", "[interaction][line]")
{
    const std::optional<std::string> person_only = "Spoke with {person}";
    const std::optional<std::string> value_only = "Played {value}";
    const std::optional<std::string> both = "Gave {person} {value}";
    const std::optional<std::string> none = "Greeted";

    CHECK(render_interaction_text(person_only, "Spoke with", ALEX, std::nullopt) == "Spoke with Alex Sample");
    CHECK(render_interaction_text(value_only, "Played song", PERSON_NOT_SET, "  Morning Medley \n") ==
          "Played Morning Medley");
    CHECK(render_interaction_text(both, "Gave", ALEX, "a card") == "Gave Alex Sample a card");
    CHECK(render_interaction_text(none, "Greeted", ALEX, "ignored") == "Greeted");
    CHECK(render_interaction_text(value_only, "Played song", ALEX, std::nullopt) == "Played ");

    // The value is never expanded again, and a literal brace is written twice.
    CHECK(render_interaction_text(value_only, "Played song", ALEX, "{person}") == "Played {person}");
    CHECK(render_interaction_text(std::optional<std::string>("{{{person}}}"), "x", ALEX, std::nullopt) ==
          "{Alex Sample}");
}

TEST_CASE("interaction.line_render: a person who is not set reads as such", "[interaction][line]")
{
    const std::optional<std::string> pattern = "Spoke with {person}";
    const Names nobody;
    CHECK(render_interaction_text(pattern, "Spoke with", people_at(nobody, NameLevel::FULL), std::nullopt) ==
          "Spoke with (person not set)");
    // Not set is not a name, so it reads the same at every level.
    CHECK(people_at(nobody, NameLevel::INITIALS) == "(person not set)");
    CHECK(people_at(nobody, NameLevel::HIDDEN) == "(person not set)");
}

TEST_CASE("interaction.line_render: patterns are validated", "[interaction][line]")
{
    CHECK(validate_interaction_pattern("Spoke with {person}").has_value());
    CHECK(validate_interaction_pattern("Played {value}").has_value());
    CHECK(validate_interaction_pattern("Gave {person} {value}").has_value());
    CHECK(validate_interaction_pattern("Greeted").has_value());
    CHECK(validate_interaction_pattern("Braces {{ and }}").has_value());

    CHECK(validate_interaction_pattern("Spoke with {person} and {person}").error() ==
          InteractionPatternError::REPEATED_PLACEHOLDER);
    CHECK(validate_interaction_pattern("{value} then {value}").error() ==
          InteractionPatternError::REPEATED_PLACEHOLDER);
    CHECK(validate_interaction_pattern("Spoke with {name}").error() == InteractionPatternError::UNKNOWN_PLACEHOLDER);
    CHECK(validate_interaction_pattern("Spoke with {person").error() == InteractionPatternError::UNBALANCED_BRACE);
    CHECK(validate_interaction_pattern("Spoke with }").error() == InteractionPatternError::UNBALANCED_BRACE);
    CHECK(validate_interaction_pattern("").error() == InteractionPatternError::EMPTY);
}

TEST_CASE("interaction.line_render: what a pattern asks for", "[interaction][line]")
{
    CHECK(interaction_asks_for(std::nullopt) == InteractionAsks::NOTHING);
    CHECK(interaction_asks_for(std::string("Greeted")) == InteractionAsks::NOTHING);
    CHECK(interaction_asks_for(std::string("Spoke with {person}")) == InteractionAsks::PERSON);
    CHECK(interaction_asks_for(std::string("Played {value}")) == InteractionAsks::TEXT);
    CHECK(interaction_asks_for(std::string("Gave {person} {value}")) == InteractionAsks::PERSON);
    for (const auto asks : {InteractionAsks::NOTHING, InteractionAsks::PERSON, InteractionAsks::TEXT})
    {
        CHECK(interaction_asks_from_key(interaction_asks_key(asks)) == asks);
    }
    CHECK_FALSE(interaction_asks_from_key("everyone").has_value());
}

TEST_CASE("interaction.line_render: the export line has the label and no ticket marker", "[interaction][line]")
{
    CHECK(render_interaction_export_line("10:12", "(Interaction)", "Spoke with Alex Sample", std::nullopt) ==
          "10:12 (Interaction) Spoke with Alex Sample");
    CHECK(render_interaction_export_line("10:48", "(Interaction)", "Visitor interaction",
                                         std::string("asked about\n the   schedule")) ==
          "10:48 (Interaction) Visitor interaction \xE2\x80\x93 asked about the schedule");
    // An empty label is dropped with its space; an empty note adds nothing.
    CHECK(render_interaction_export_line("10:12", "", "Greeted", std::string("  ")) == "10:12 Greeted");
    CHECK(export_format::DEFAULT_INTERACTION_MARKER == "(Interaction)");
}

TEST_CASE("interaction.name_levels: full, initials and hidden", "[interaction][name]")
{
    CHECK(person_name_at_level("Alex Sample", NameLevel::FULL) == "Alex Sample");
    CHECK(person_name_at_level("Alex Sample", NameLevel::INITIALS) == "A.S.");
    CHECK(person_name_at_level("Alex Sample", NameLevel::HIDDEN) == "someone");

    // One word, more than two words, hyphens, lower case typed names.
    CHECK(person_initials("Madonna") == "M.");
    CHECK(person_initials("Mary-Jane Watson") == "M.J.W.");
    CHECK(person_initials("pat stand-in") == "P.S.I.");
    CHECK(person_initials("Pat Stand-in") == "P.S.I.");
    CHECK(person_initials("\xC3\xA9mile Sample") == "\xC3\x89.S."); // an accented capital
    // Non-Latin scripts: the first character of each word.
    CHECK(person_initials("\xE6\x9D\x8E \xE5\xB0\x8F\xE9\xBE\x8D") == "\xE6\x9D\x8E.\xE5\xB0\x8F.");
    CHECK(
        person_initials("\xD0\x90\xD0\xBB\xD0\xB5\xD0\xBA\xD1\x81 \xD0\x9F\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD1\x80") ==
        "\xD0\x90.\xD0\x9F.");
    // Outside the basic plane: one code point, not half of a surrogate pair.
    CHECK(person_initials("\xF0\x9F\x99\x82 Sample") == "\xF0\x9F\x99\x82.S.");
    // No letters at all falls back to "someone" rather than an empty string.
    CHECK(person_name_at_level("-", NameLevel::INITIALS) == "someone");

    for (const auto level : {NameLevel::FULL, NameLevel::INITIALS, NameLevel::HIDDEN})
    {
        CHECK(name_level_from_key(name_level_key(level)) == level);
    }
    CHECK_FALSE(name_level_from_key("nicknames").has_value());
}

TEST_CASE("interaction.group_render: one, two and three people in pick order", "[interaction][group]")
{
    const Names one{ALEX};
    const Names two{ALEX, JORDAN};
    const Names three{ALEX, JORDAN, PAT};
    CHECK(people_at(one, NameLevel::FULL) == "Alex Sample");
    CHECK(people_at(two, NameLevel::FULL) == "Alex Sample and Jordan Placeholder");
    CHECK(people_at(three, NameLevel::FULL) == "Alex Sample, Jordan Placeholder and Pat Stand-in");
    const Names reversed{PAT, ALEX};
    CHECK(people_at(reversed, NameLevel::FULL) == "Pat Stand-in and Alex Sample");

    // Each name is shortened separately; hidden is one word however many people there are.
    CHECK(people_at(three, NameLevel::INITIALS) == "A.S., J.P. and P.S.I.");
    CHECK(people_at(two, NameLevel::INITIALS) == "A.S. and J.P.");
    CHECK(people_at(one, NameLevel::HIDDEN) == "someone");
    CHECK(people_at(three, NameLevel::HIDDEN) == "someone");

    // A forgotten person has no name and reads as someone.
    const Names forgotten{ALEX, std::nullopt};
    CHECK(people_at(forgotten, NameLevel::FULL) == "Alex Sample and someone");

    const std::optional<std::string> pattern = "Spoke with {person}";
    CHECK(render_interaction_text(pattern, "Spoke with", people_at(three, NameLevel::FULL), std::nullopt) ==
          "Spoke with Alex Sample, Jordan Placeholder and Pat Stand-in");
}

TEST_CASE("interaction.group_render: at most twenty people, repeats ignored", "[interaction][group]")
{
    std::vector<PersonId> people;
    for (std::uint64_t i = 1; i <= MAX_PEOPLE_PER_INTERACTION; ++i)
    {
        REQUIRE(add_person_pick(people, sample_id<PersonId>(i)).has_value());
    }
    CHECK(people.size() == 20);
    CHECK(add_person_pick(people, sample_id<PersonId>(21)).error() == PickError::TOO_MANY_PEOPLE);
    CHECK(people.size() == 20);

    // A duplicate is ignored, also when the list is full, and does not move the first pick.
    CHECK(add_person_pick(people, sample_id<PersonId>(1)).has_value());
    CHECK(people.size() == 20);
    CHECK(people.front() == sample_id<PersonId>(1));

    std::vector<PersonId> few;
    REQUIRE(add_person_pick(few, sample_id<PersonId>(2)).has_value());
    REQUIRE(add_person_pick(few, sample_id<PersonId>(1)).has_value());
    REQUIRE(add_person_pick(few, sample_id<PersonId>(2)).has_value());
    CHECK(few == std::vector<PersonId>{sample_id<PersonId>(2), sample_id<PersonId>(1)});
}

TEST_CASE("interaction.group_render: a stored interaction obeys the same limits", "[interaction][group]")
{
    const auto stored = [](const Entry& entry)
    {
        auto log = make_stored_log(sample_id<LogId>(100), CreateLogBody{sample_header(), sample_time(0), std::nullopt});
        REQUIRE(log.has_value());
        return apply_record(*log, interaction_record(2, entry), ApplyMode::STRICT);
    };

    CHECK(stored(sample_interaction(1, 5)).has_value());

    Entry twenty = sample_interaction(1, 5);
    auto& people = std::get<InteractionEntry>(twenty.m_Body).m_People;
    people.clear();
    for (std::uint64_t i = 1; i <= 20; ++i)
    {
        people.push_back(sample_id<PersonId>(i));
    }
    CHECK(stored(twenty).has_value());

    people.push_back(sample_id<PersonId>(21));
    CHECK(stored(twenty).error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);

    Entry repeated = sample_interaction(1, 5);
    std::get<InteractionEntry>(repeated.m_Body).m_People = {sample_id<PersonId>(1), sample_id<PersonId>(1)};
    CHECK(stored(repeated).error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);

    Entry no_label = sample_interaction(1, 5);
    std::get<InteractionEntry>(no_label.m_Body).m_TypeLabel.clear();
    CHECK(stored(no_label).error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);

    Entry long_value = sample_interaction(1, 5);
    std::get<InteractionEntry>(long_value.m_Body).m_Value = std::string(MAX_INTERACTION_VALUE_CODE_POINTS + 1, 'x');
    CHECK(stored(long_value).error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);

    Entry long_note = sample_interaction(1, 5);
    std::get<InteractionEntry>(long_note.m_Body).m_AttachedNote = std::string(MAX_NOTE_CODE_POINTS + 1, 'x');
    CHECK(stored(long_note).error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);

    Entry no_people = sample_interaction(1, 5); // "log first, name second": an entry with nobody set is normal
    std::get<InteractionEntry>(no_people.m_Body).m_People.clear();
    CHECK(stored(no_people).has_value());
}

TEST_CASE("journal.v2_roundtrip: an interaction is written at version 2 and reads back", "[interaction][journal]")
{
    const JournalRecord record = interaction_record(2, sample_interaction(1, 5));
    CHECK(record.m_Version == 2);
    const auto line = encode_record(record);
    REQUIRE(line.has_value());
    CHECK(line->starts_with("{\"v\":2,\"seq\":2,\"op\":\"add_entry\","));
    CHECK(line->find("\"kind\":\"interaction\"") != std::string::npos);

    const auto decoded = decode_record(*line);
    REQUIRE(decoded.has_value());
    CHECK(decoded->m_Version == 2);
    const auto& entry = std::get<AddEntryBody>(decoded->m_Body).m_Entry;
    const auto& body = std::get<InteractionEntry>(entry.m_Body);
    const auto& expected = std::get<InteractionEntry>(std::get<AddEntryBody>(record.m_Body).m_Entry.m_Body);
    CHECK(body.m_Type == expected.m_Type);
    CHECK(body.m_TypeLabel == "Spoke with");
    CHECK(body.m_People == expected.m_People);
    CHECK(body.m_Value == "Morning Medley");
    CHECK(body.m_AttachedNote == "asked about \"the schedule\"");
    CHECK(encode_record(*decoded).value() == *line);

    // An edit carries the whole entry and is version 2 as well. No people is an empty list, and null values read back.
    Entry bare = sample_interaction(1, 5);
    auto& bare_body = std::get<InteractionEntry>(bare.m_Body);
    bare_body.m_People.clear();
    bare_body.m_Value.reset();
    bare_body.m_AttachedNote.reset();
    JournalBody edit = EditEntryBody{bare, sample_time(9)};
    const JournalRecord edit_record{journal_version_for(edit), 3, sample_id<LogId>(100), edit};
    const auto edit_line = encode_record(edit_record);
    REQUIRE(edit_line.has_value());
    CHECK(edit_line->starts_with("{\"v\":2,\"seq\":3,\"op\":\"edit_entry\","));
    CHECK(edit_line->find("\"people\":[]") != std::string::npos);
    CHECK(edit_line->find("\"value\":null,\"note\":null") != std::string::npos);
    const auto edit_decoded = decode_record(*edit_line);
    REQUIRE(edit_decoded.has_value());
    CHECK(encode_record(*edit_decoded).value() == *edit_line);

    // The person ids are the whole of a person in a log: no name appears in the line.
    CHECK(line->find("Alex") == std::string::npos);
}

TEST_CASE("journal.v2_roundtrip: records of every other kind stay at version 1 and decode as before",
          "[interaction][journal]")
{
    CHECK(journal_version_for(AddEntryBody{sample_error(1, 30)}) == 1);
    CHECK(journal_version_for(AddEntryBody{sample_note(1, 30, "free")}) == 1);
    CHECK(journal_version_for(EditEntryBody{sample_error(1, 30), sample_time(40)}) == 1);
    CHECK(journal_version_for(ArchiveBody{}) == 1);

    const auto line = encode_record(sample_record(2, AddEntryBody{sample_error(1, 30)}));
    REQUIRE(line.has_value());
    CHECK(line->starts_with("{\"v\":1,"));
    CHECK(decode_record(*line).has_value());

    // A hand-written version 1 line, as an older build wrote it, still reads.
    const std::string v1 =
        with_crc("{\"v\":1,\"seq\":2,\"op\":\"archive\",\"log\":\"00000000-0000-4000-8000-000000000064\",\"body\":{}}");
    const auto old = decode_record(v1);
    REQUIRE(old.has_value());
    CHECK(old->m_Version == 1);
}

TEST_CASE("journal.v2_roundtrip: the version must fit the record", "[interaction][journal]")
{
    // An interaction written at version 1 or another kind written at version 2 is refused on write...
    JournalRecord wrong = interaction_record(2, sample_interaction(1, 5));
    wrong.m_Version = 1;
    CHECK(encode_record(wrong).error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);
    JournalRecord other = sample_record(2, AddEntryBody{sample_error(1, 30)});
    other.m_Version = 2;
    CHECK_FALSE(encode_record(other).has_value());

    // ...and a line that says so is not read as a record, though its checksum is good.
    const auto line = encode_record(interaction_record(2, sample_interaction(1, 5)));
    REQUIRE(line.has_value());
    std::string as_v1 = line->substr(0, line->size() - CRC_SUFFIX_BYTES); // without ,"crc":"xxxxxxxx"}
    as_v1 += "}";
    as_v1.replace(as_v1.find("\"v\":2"), 5, "\"v\":1");
    REQUIRE(as_v1.starts_with("{\"v\":1,"));
    CHECK(decode_record(with_crc(as_v1)).error() == DecodeError::MALFORMED);

    std::string as_v2 =
        with_crc("{\"v\":2,\"seq\":2,\"op\":\"archive\",\"log\":\"00000000-0000-4000-8000-000000000064\",\"body\":{}}");
    CHECK_FALSE(decode_record(as_v2).has_value());

    // Version 3 is from a newer build and is not damage.
    const std::string future =
        with_crc("{\"v\":3,\"seq\":2,\"op\":\"archive\",\"log\":\"00000000-0000-4000-8000-000000000064\",\"body\":{}}");
    CHECK(decode_record(future).error() == DecodeError::UNSUPPORTED_VERSION);
}

TEST_CASE("journal.v2_roundtrip: damaged interaction records are rejected", "[interaction][journal]")
{
    const auto line = encode_record(interaction_record(2, sample_interaction(1, 5)));
    REQUIRE(line.has_value());

    // A flipped bit anywhere is never a record.
    for (std::size_t i = 0; i < line->size(); i += 7)
    {
        std::string damaged = *line;
        damaged[i] = static_cast<char>(damaged[i] ^ 0x01);
        CHECK_FALSE(decode_record(damaged).has_value());
    }
    // A person id that is not a UUID is a bad field.
    std::string bad = line->substr(0, line->size() - CRC_SUFFIX_BYTES) + "}";
    const auto first = bad.find("\"people\":[\"") + 11;
    bad.replace(first, 36, "not-a-uuid-not-a-uuid-not-a-uuid-xxx");
    CHECK(decode_record(with_crc(bad)).error() == DecodeError::BAD_FIELD);
}

TEST_CASE("journal.v2_roundtrip: the longest interaction stays under the record bound", "[interaction][journal]")
{
    // The longest note, answer and label together, every character a six byte escape, and twenty people.
    Entry worst = sample_interaction(1, 5);
    auto& body = std::get<InteractionEntry>(worst.m_Body);
    body.m_TypeLabel = std::string(MAX_LABEL_CODE_POINTS, '\x01');
    body.m_Value = std::string(MAX_INTERACTION_VALUE_CODE_POINTS, '\x01');
    body.m_AttachedNote = std::string(MAX_NOTE_CODE_POINTS, '\x01');
    body.m_People.clear();
    for (std::uint64_t i = 1; i <= MAX_PEOPLE_PER_INTERACTION; ++i)
    {
        body.m_People.push_back(sample_id<PersonId>(i));
    }
    worst.m_Captured.m_Zone = std::string(MAX_ZONE_BYTES, 'z');
    worst.m_EditedUtc = sample_time(9);

    JournalBody edit = EditEntryBody{worst, sample_time(9)};
    const auto line = encode_record(JournalRecord{journal_version_for(edit), 99'999, sample_id<LogId>(100), edit});
    REQUIRE(line.has_value());
    CHECK(line->size() < MAX_RECORD_BYTES);
    const auto decoded = decode_record(*line);
    REQUIRE(decoded.has_value());
    CHECK(std::get<InteractionEntry>(std::get<EditEntryBody>(decoded->m_Body).m_Entry.m_Body).m_People.size() == 20);

    // Four byte characters are the widest that need no escape, and also fit.
    body.m_TypeLabel.clear();
    for (std::size_t i = 0; i < MAX_LABEL_CODE_POINTS; ++i)
    {
        body.m_TypeLabel += "\xF0\x9F\x99\x82";
    }
    body.m_Value = std::string();
    for (std::size_t i = 0; i < MAX_INTERACTION_VALUE_CODE_POINTS; ++i)
    {
        body.m_Value->append("\xF0\x9F\x99\x82");
    }
    body.m_AttachedNote = std::string();
    for (std::size_t i = 0; i < MAX_NOTE_CODE_POINTS; ++i)
    {
        body.m_AttachedNote->append("\xF0\x9F\x99\x82");
    }
    JournalBody wide = EditEntryBody{worst, sample_time(9)};
    const auto wide_line = encode_record(JournalRecord{journal_version_for(wide), 99'999, sample_id<LogId>(100), wide});
    REQUIRE(wide_line.has_value());
    CHECK(wide_line->size() < MAX_RECORD_BYTES);
}

TEST_CASE("export.interaction_lines: an interaction renders at the name level of the context", "[interaction][export]")
{
    Log log(sample_header());
    REQUIRE(log.add(sample_interaction(1, 5)).has_value());
    ExportContext context;
    const FakeTimeZoneProvider zones({{"Europe/London", std::chrono::minutes(0)}});
    context.m_Zones = &zones;
    context.m_PersonName = [](const PersonId& id) -> std::optional<std::string>
    {
        if (id == sample_id<PersonId>(1))
        {
            return std::string(ALEX);
        }
        if (id == sample_id<PersonId>(2))
        {
            return std::string(JORDAN);
        }
        return std::nullopt;
    };
    context.m_InteractionPattern = [](InteractionTypeId) -> std::optional<std::string>
    { return std::string("Spoke with {person}"); };

    const auto line_at = [&](NameLevel level)
    {
        context.m_NameLevel = level;
        return render_entry_line(log, sample_id<EntryId>(1), context);
    };
    // The note is the attached one, and there is no ticket marker.
    CHECK(line_at(NameLevel::FULL).value() ==
          "00:05 (Interaction) Spoke with Alex Sample and Jordan Placeholder \xE2\x80\x93 asked about \"the schedule\"");
    CHECK(line_at(NameLevel::INITIALS).value() ==
          "00:05 (Interaction) Spoke with A.S. and J.P. \xE2\x80\x93 asked about \"the schedule\"");
    CHECK(line_at(NameLevel::HIDDEN).value() ==
          "00:05 (Interaction) Spoke with someone \xE2\x80\x93 asked about \"the schedule\"");
    // An empty label drops the label and its space.
    context.m_InteractionMarker = "";
    CHECK(line_at(NameLevel::INITIALS).value() ==
          "00:05 Spoke with A.S. and J.P. \xE2\x80\x93 asked about \"the schedule\"");
}

TEST_CASE("export.interaction_lines: the type is looked up only when the entry has no snapshot", "[interaction][export]")
{
    Log log(sample_header());
    Entry entry = sample_interaction(1, 5);
    std::get<InteractionEntry>(entry.m_Body).m_TypeLabel.clear();
    REQUIRE(log.add(std::move(entry)).has_value());
    ExportContext context;
    const FakeTimeZoneProvider zones({{"Europe/London", std::chrono::minutes(0)}});
    context.m_Zones = &zones;
    CHECK(render_entry_line(log, sample_id<EntryId>(1), context).error() == RenderError::UNKNOWN_INTERACTION_TYPE);
    context.m_InteractionTypeName = [](InteractionTypeId) -> std::string_view { return "Spoke with"; };
    // No pattern and no person source: the line is the name, and a person nobody can name is not printed.
    CHECK(render_entry_line(log, sample_id<EntryId>(1), context).value() ==
          "00:05 (Interaction) Spoke with \xE2\x80\x93 asked about \"the schedule\"");
}

TEST_CASE("export.interaction_lines: error lines are byte for byte as they were without interactions",
          "[interaction][export]")
{
    // The downstream ticket indexer reads the `T|` lines, so an error line must not change with the name level, the
    // label, or an interaction beside it in the log.
    const FakeTimeZoneProvider zones(
        {{"Europe/London", std::chrono::minutes(0)}, {"Australia/Melbourne", std::chrono::minutes(0)}});
    Log log(sample_header());
    REQUIRE(log.add(sample_error(1, 3)).has_value());
    REQUIRE(log.add(sample_interaction(2, 5)).has_value());
    REQUIRE(log.add(sample_error(3, 20, "Mute Delay", "heard it twice")).has_value());

    ExportContext context;
    context.m_Zones = &zones;
    const auto first = render_entry_line(log, sample_id<EntryId>(1), context);
    const auto second = render_entry_line(log, sample_id<EntryId>(3), context);
    REQUIRE(first.has_value());
    REQUIRE(second.has_value());
    CHECK(*first == "00:03 T| Wrong Response");
    CHECK(*second == "00:20 T| Mute Delay \xE2\x80\x93 heard it twice");

    for (const NameLevel level : {NameLevel::FULL, NameLevel::INITIALS, NameLevel::HIDDEN})
    {
        for (const std::string_view marker : {"(Interaction)", "", "(Chat)"})
        {
            context.m_NameLevel = level;
            context.m_InteractionMarker = marker;
            context.m_InteractionPattern = [](InteractionTypeId) -> std::optional<std::string>
            { return std::string("Spoke with {person}"); };
            CHECK(render_entry_line(log, sample_id<EntryId>(1), context).value() == *first);
            CHECK(render_entry_line(log, sample_id<EntryId>(3), context).value() == *second);
            CHECK(render_entry_line(log, sample_id<EntryId>(2), context).value().find("T|") == std::string::npos);
        }
    }
}
