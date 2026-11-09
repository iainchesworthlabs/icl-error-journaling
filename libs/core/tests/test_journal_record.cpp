// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/journal_record.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;

namespace
{

/// A line from the text of an object without its crc key (what the checksum covers): adds the key with the right
/// checksum, as a writer would.
std::string with_crc(const std::string& object)
{
    char digits[16];
    std::snprintf(digits, sizeof digits, "%08x", crc32(object));
    return object.substr(0, object.size() - 1) + ",\"crc\":\"" + digits + "\"}";
}

std::string encoded(const JournalRecord& record)
{
    const auto line = encode_record(record);
    REQUIRE(line.has_value());
    return *line;
}

std::vector<JournalRecord> every_op()
{
    HeaderValues bare;
    bare.m_Facilitator = "Sam Example";
    bare.m_LocalDate = std::chrono::year{2026} / 11 / 8;  // no unit, location or version

    HeaderValues by_id = sample_header();
    by_id.m_Location = LocationValue{sample_id<LocationId>(5), "North Site"};

    Entry edited = sample_error(2, 31, "Wrong Response", "robot stopped; note with \"quotes\" and \\ and \n");
    edited.m_EditedUtc = sample_time(45);
    edited.m_Deleted = true;

    Entry templated = sample_note(3, 32, "Played Sam's song");
    std::get<NoteEntry>(templated.m_Body).m_Template = sample_id<NoteTemplateId>(9);

    return {
        sample_record(1, CreateLogBody{sample_header(), sample_time(0), std::nullopt}),
        sample_record(1, CreateLogBody{bare, sample_time(1), sample_id<LogId>(99)}),
        sample_record(1, CreateLogBody{by_id, sample_time(1), std::nullopt}),
        sample_record(2, AddEntryBody{sample_error(1, 30)}),
        sample_record(3, AddEntryBody{edited}),
        sample_record(4, AddEntryBody{templated}),
        sample_record(5, AddEntryBody{sample_note(4, 33, "free text")}),
        sample_record(6, AddEntryBody{sample_change(5, 34, HeaderField::UNIT_NUMBER, "28")}),
        sample_record(7, EditEntryBody{sample_error(1, 35, "Other"), sample_time(60)}),
        sample_record(8, SetDeletedBody{sample_id<EntryId>(1), true}),
        sample_record(9, SetDeletedBody{sample_id<EntryId>(1), false}),
        sample_record(10, CorrectHeaderBody{HeaderField::SOFTWARE_VERSION, "1.4.3"}),
        sample_record(11, CorrectHeaderBody{HeaderField::LOCATION, ""}),
        sample_record(12, MarkSharedBody{sample_time(90)}),
        sample_record(13, ArchiveBody{}),
        sample_record(14, VoidBody{12}),
    };
}

}  // namespace

TEST_CASE("crc32 is the IEEE checksum and continues from an earlier value", "[journal]")
{
    CHECK(crc32("") == 0);
    CHECK(crc32("123456789") == 0xCBF43926U);
    CHECK(crc32("456789", crc32("123")) == 0xCBF43926U);
    CHECK(crc32("a") != crc32("b"));
}

TEST_CASE("A record is one line with the keys in a fixed order and a checksum of the rest", "[journal]")
{
    const auto record = sample_record(42, SetDeletedBody{sample_id<EntryId>(7), true});
    const std::string line = encoded(record);
    CHECK(
        line.starts_with("{\"v\":1,\"seq\":42,\"op\":\"set_deleted\",\"log\":\"00000000-0000-4000-8000-000000000064\","
                         "\"body\":{\"entry\":\"00000000-0000-4000-8000-000000000007\",\"deleted\":true},\"crc\":\""));
    CHECK(line.ends_with("\"}"));
    CHECK(line.find('\n') == std::string::npos);

    // The checksum covers the object without the crc key.
    const std::string object = line.substr(0, line.size() - 18) + "}";
    CHECK(line == with_crc(object));
}

TEST_CASE("Every operation survives a round trip and encodes the same way again", "[journal]")
{
    for (const JournalRecord& record : every_op())
    {
        const std::string line = encoded(record);
        INFO(line);
        const auto decoded = decode_record(line);
        REQUIRE(decoded.has_value());
        CHECK(decoded->m_Seq == record.m_Seq);
        CHECK(decoded->m_Log == record.m_Log);
        CHECK(decoded->m_Version == JOURNAL_VERSION);
        CHECK(journal_op(decoded->m_Body) == journal_op(record.m_Body));
        CHECK(encoded(*decoded) == line);
    }
}

TEST_CASE("A decoded record holds the values that were written", "[journal]")
{
    Entry labelled = sample_error(2, 31, "Wrong Response", "note \"quoted\" \\ and \n newline");
    labelled.m_EditedUtc = sample_time(45);
    labelled.m_Deleted = true;
    const auto decoded = decode_record(encoded(sample_record(3, AddEntryBody{labelled})));
    REQUIRE(decoded.has_value());
    const auto& body = std::get<AddEntryBody>(decoded->m_Body);
    CHECK(std::get<ErrorEntry>(body.m_Entry.m_Body).m_ErrorTypeLabel == "Wrong Response");
    CHECK(body.m_Entry.m_Id == sample_id<EntryId>(2));
    CHECK(body.m_Entry.m_Captured.m_Utc == sample_time(31));
    CHECK(body.m_Entry.m_Captured.m_Zone == "Australia/Melbourne");
    CHECK(body.m_Entry.m_EditedUtc == sample_time(45));
    CHECK(body.m_Entry.m_Deleted);
    const auto& error = std::get<ErrorEntry>(body.m_Entry.m_Body);
    CHECK(error.m_ErrorType == sample_id<ErrorTypeId>(1));
    CHECK(error.m_AttachedNote == "note \"quoted\" \\ and \n newline");

    const auto created =
        decode_record(encoded(sample_record(1, CreateLogBody{sample_header(), sample_time(3), sample_id<LogId>(8)})));
    REQUIRE(created.has_value());
    const auto& create = std::get<CreateLogBody>(created->m_Body);
    CHECK(create.m_Header.m_Facilitator == "Sam Example");
    CHECK(create.m_Header.m_LocalDate == std::chrono::year{2026} / 11 / 7);
    CHECK(create.m_Header.m_UnitNumber->digits() == "27");
    CHECK(create.m_Header.m_Location->m_Text == "North Site");
    CHECK_FALSE(create.m_Header.m_Location->m_Id.has_value());
    CHECK(create.m_Header.m_SoftwareVersion->format() == "1.4.2");
    CHECK(create.m_CreatedUtc == sample_time(3));
    CHECK(create.m_Previous == sample_id<LogId>(8));
}

TEST_CASE("Text with controls, quotes, NUL and non-ASCII characters round trips", "[journal]")
{
    const std::string nasty =
        std::string("a\0b", 3) + "\x01\x1f\t\r\b\f \"\\/ caf\xC3\xA9 \xE2\x86\x92 \xF0\x9F\x98\x80 \x7f";
    const auto decoded = decode_record(encoded(sample_record(2, AddEntryBody{sample_note(1, 1, nasty)})));
    REQUIRE(decoded.has_value());
    CHECK(std::get<NoteEntry>(std::get<AddEntryBody>(decoded->m_Body).m_Entry.m_Body).m_Value == nasty);
}

TEST_CASE("A flipped bit anywhere in a line is never read as a record", "[journal]")
{
    for (const JournalRecord& record : every_op())
    {
        const std::string line = encoded(record);
        for (std::size_t i = 0; i < line.size(); ++i)
        {
            for (int bit = 0; bit < 8; ++bit)
            {
                std::string damaged = line;
                damaged[i] = static_cast<char>(static_cast<unsigned char>(damaged[i]) ^ (1U << bit));
                INFO("byte " << i << " bit " << bit << " of " << line);
                CHECK_FALSE(decode_record(damaged).has_value());
            }
        }
    }
}

TEST_CASE("A line cut short is never read as a record", "[journal]")
{
    const std::string line = encoded(sample_record(2, AddEntryBody{sample_error(1, 30)}));
    for (std::size_t length = 0; length < line.size(); ++length)
    {
        CHECK_FALSE(decode_record(std::string_view(line).substr(0, length)).has_value());
    }
}

TEST_CASE("A checksum mismatch is reported as one", "[journal]")
{
    std::string line = encoded(sample_record(2, ArchiveBody{}));
    line[line.size() - 5] = line[line.size() - 5] == '0' ? '1' : '0';
    CHECK(decode_record(line).error() == DecodeError::BAD_CRC);

    std::string body_changed = encoded(sample_record(2, SetDeletedBody{sample_id<EntryId>(7), true}));
    body_changed.replace(body_changed.find("true"), 4, "fals");
    CHECK(decode_record(body_changed).error() == DecodeError::BAD_CRC);
}

TEST_CASE("Lines that are not records are rejected", "[journal]")
{
    CHECK(decode_record("").error() == DecodeError::MALFORMED);
    CHECK(decode_record("not json at all, and long enough to carry a suffix").error() == DecodeError::MALFORMED);
    CHECK(decode_record(std::string(300, '\0')).error() == DecodeError::MALFORMED);
    CHECK(decode_record(with_crc("[1,2,3]")).error() == DecodeError::MALFORMED);
    CHECK(decode_record(with_crc("{\"v\":1}")).error() == DecodeError::MALFORMED);
    // A line that is not UTF-8 (J8)
    CHECK_FALSE(
        decode_record(with_crc("{\"v\":1,\"seq\":2,\"op\":\"archive\",\"log\":\"\xFF\xFE\",\"body\":{}}")).has_value());
}

TEST_CASE("A newer version with a good checksum is reported as unsupported, not as damage", "[journal]")
{
    const std::string line =
        with_crc("{\"v\":2,\"seq\":2,\"op\":\"archive\",\"log\":\"00000000-0000-4000-8000-000000000064\",\"body\":{}}");
    CHECK(decode_record(line).error() == DecodeError::UNSUPPORTED_VERSION);
    const std::string zero =
        with_crc("{\"v\":0,\"seq\":2,\"op\":\"archive\",\"log\":\"00000000-0000-4000-8000-000000000064\",\"body\":{}}");
    CHECK(decode_record(zero).error() == DecodeError::MALFORMED);
}

TEST_CASE("A record with a valid checksum but an invalid value is a bad field", "[journal]")
{
    const std::string log = "\"log\":\"00000000-0000-4000-8000-000000000064\"";
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":2,\"op\":\"set_deleted\"," + log +
                                 ",\"body\":{\"entry\":\"not-a-uuid\",\"deleted\":true}}"))
              .error() == DecodeError::BAD_FIELD);
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":2,\"op\":\"correct_header\"," + log +
                                 ",\"body\":{\"field\":\"colour\",\"value\":\"x\"}}"))
              .error() == DecodeError::BAD_FIELD);
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":2,\"op\":\"void\"," + log + ",\"body\":{\"target\":0}}")).error() ==
          DecodeError::BAD_FIELD);
    const std::string header = "{\"header\":{\"facilitator\":\"Sam Example\",\"date\":\"2026-02-30\",\"unit\":null,"
                               "\"location\":null,\"version\":null},\"created\":0,\"prev\":null}";
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":1,\"op\":\"create_log\"," + log + ",\"body\":" + header + "}"))
              .error() == DecodeError::BAD_FIELD);
    const std::string unit = "{\"header\":{\"facilitator\":\"Sam Example\",\"date\":\"2026-02-03\",\"unit\":\"0x\","
                             "\"location\":null,\"version\":null},\"created\":0,\"prev\":null}";
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":1,\"op\":\"create_log\"," + log + ",\"body\":" + unit + "}"))
              .error() == DecodeError::BAD_FIELD);
}

TEST_CASE("A record that is valid JSON but not the encoder's exact text is rejected", "[journal]")
{
    // Upper-case UUID, an extra space, an equivalent escape, a different key order: each parses, none is canonical.
    const std::string good =
        "{\"v\":1,\"seq\":2,\"op\":\"set_deleted\",\"log\":\"00000000-0000-4000-8000-000000000064\","
        "\"body\":{\"entry\":\"00000000-0000-4000-8000-000000000007\",\"deleted\":true}}";
    CHECK(decode_record(with_crc(good)).has_value());

    const std::string lower_letters = "00000000-0000-4000-8000-0000000000ab";
    std::string with_letters = good;
    with_letters.replace(with_letters.find("00000000-0000-4000-8000-000000000007"), 36, lower_letters);
    CHECK(decode_record(with_crc(with_letters)).has_value());
    std::string upper = good;
    upper.replace(upper.find("00000000-0000-4000-8000-000000000007"), 36, "00000000-0000-4000-8000-0000000000AB");
    CHECK_FALSE(decode_record(with_crc(upper)).has_value());

    std::string spaced = good;
    spaced.replace(spaced.find(":true"), 5, ": true");
    CHECK_FALSE(decode_record(with_crc(spaced)).has_value());

    std::string reordered =
        "{\"seq\":2,\"v\":1,\"op\":\"set_deleted\",\"log\":\"00000000-0000-4000-8000-000000000064\","
        "\"body\":{\"entry\":\"00000000-0000-4000-8000-000000000007\",\"deleted\":true}}";
    CHECK_FALSE(decode_record(with_crc(reordered)).has_value());

    std::string escaped = good;
    escaped.replace(escaped.find("set_deleted"), 11, "set\\u005fdeleted");
    CHECK_FALSE(decode_record(with_crc(escaped)).has_value());

    std::string extra = good;
    extra.insert(extra.size() - 1, ",\"extra\":1");
    CHECK_FALSE(decode_record(with_crc(extra)).has_value());
}

TEST_CASE("The size of a record is bounded", "[journal]")
{
    // The longest legal note encodes well inside the bound, even when every character needs a six byte escape.
    const std::string longest_legal(10'000, '\x01');
    const auto legal = encode_record(sample_record(2, AddEntryBody{sample_note(1, 1, longest_legal)}));
    REQUIRE(legal.has_value());
    CHECK(legal->size() < MAX_RECORD_BYTES);
    CHECK(decode_record(*legal).has_value());

    // A record beyond the bound is refused on write, and a line beyond it is refused on read without being parsed.
    const auto too_big = encode_record(sample_record(2, AddEntryBody{sample_note(1, 1, std::string(30'000, '\x01'))}));
    REQUIRE_FALSE(too_big.has_value());
    CHECK(too_big.error().m_Kind == StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(decode_record(std::string(MAX_RECORD_BYTES + 1, 'x')).error() == DecodeError::TOO_LONG);
    CHECK(decode_record(std::string(10 * 1024 * 1024, 'x')).error() == DecodeError::TOO_LONG);
}

TEST_CASE("A record that cannot be written as JSON is refused", "[journal]")
{
    const auto bad_text = encode_record(sample_record(2, AddEntryBody{sample_note(1, 1, "broken \xC3")}));
    REQUIRE_FALSE(bad_text.has_value());
    CHECK(bad_text.error().m_Kind == StorageErrorKind::INVALID_ARGUMENT);

    CHECK_FALSE(encode_record(sample_record(0, ArchiveBody{})).has_value());  // seq starts at 1

    JournalRecord future = sample_record(2, ArchiveBody{});
    future.m_Version = 2;
    CHECK_FALSE(encode_record(future).has_value());

    HeaderValues far_date = sample_header();
    far_date.m_LocalDate = std::chrono::year{10'000} / 1 / 1;
    CHECK_FALSE(encode_record(sample_record(1, CreateLogBody{far_date, sample_time(0), std::nullopt})).has_value());

    Entry huge_time = sample_error(1, 0);
    huge_time.m_Captured.m_Utc = UtcMillis{std::chrono::milliseconds{std::int64_t{1} << 60}};
    CHECK_FALSE(encode_record(sample_record(2, AddEntryBody{huge_time})).has_value());
}

TEST_CASE("Times before 1970 and at 1970 round trip", "[journal]")
{
    for (const std::int64_t millis : {std::int64_t{0}, std::int64_t{-1}, std::int64_t{-86'400'000'000}})
    {
        Entry labelled = sample_error(1, 0);
        labelled.m_Captured.m_Utc = UtcMillis{std::chrono::milliseconds{millis}};
        const auto decoded = decode_record(encoded(sample_record(2, AddEntryBody{labelled})));
        REQUIRE(decoded.has_value());
        CHECK(std::get<AddEntryBody>(decoded->m_Body).m_Entry.m_Captured.m_Utc.time_since_epoch().count() == millis);
    }
}

TEST_CASE("Every operation has a name that decodes", "[journal]")
{
    CHECK(journal_op_name(JournalOp::CREATE_LOG) == "create_log");
    CHECK(journal_op_name(JournalOp::VOID_RECORD) == "void");
    for (int i = 0; i <= static_cast<int>(DecodeError::BAD_FIELD); ++i)
    {
        CHECK_FALSE(decode_error_name(static_cast<DecodeError>(i)).empty());
    }
}

TEST_CASE("A value outside the enumerations has no name", "[journal]")
{
    CHECK(journal_op_name(static_cast<JournalOp>(99)).empty());
    CHECK(decode_error_name(static_cast<DecodeError>(99)).empty());
}

TEST_CASE("An entry or header with a bad part is reported, not guessed at", "[journal]")
{
    const std::string log = "\"log\":\"00000000-0000-4000-8000-000000000064\"";
    const std::string entry_start =
        "{\"id\":\"00000000-0000-4000-8000-000000000001\",\"utc\":0,\"zone\":\"\",\"edited\":null,";
    const auto add = [&](const std::string& entry)
    {
        return decode_record(
            with_crc("{\"v\":1,\"seq\":2,\"op\":\"add_entry\"," + log + ",\"body\":{\"entry\":" + entry + "}}"));
    };

    CHECK(add(entry_start + "\"deleted\":false,\"kind\":\"error\",\"type\":\"00000000-0000-4000-8000-000000000002\","
                            "\"label\":\"Wrong Response\",\"note\":null}")
              .has_value());
    CHECK(add(entry_start + "\"deleted\":false,\"kind\":\"mystery\"}").error() == DecodeError::BAD_FIELD);
    CHECK(add(entry_start + "\"deleted\":false,\"kind\":\"header_change\",\"field\":\"colour\",\"value\":\"x\"}")
              .error() == DecodeError::BAD_FIELD);
    CHECK(add(entry_start + "\"deleted\":\"no\",\"kind\":\"note\",\"template\":null,\"value\":\"x\"}").error() ==
          DecodeError::MALFORMED);
    CHECK(add(entry_start + "\"deleted\":false,\"kind\":\"note\",\"template\":7,\"value\":\"x\"}").error() ==
          DecodeError::MALFORMED);

    const std::string header = "{\"header\":{\"facilitator\":\"Sam Example\",\"date\":\"2026-02-03\",\"unit\":null,"
                               "\"location\":null,\"version\":\"1.2\"},\"created\":0,\"prev\":null}";
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":1,\"op\":\"create_log\"," + log + ",\"body\":" + header + "}"))
              .error() == DecodeError::BAD_FIELD);
    const std::string shaped = "{\"header\":{\"facilitator\":\"Sam Example\",\"date\":\"2026/02/03\",\"unit\":null,"
                               "\"location\":null,\"version\":null},\"created\":0,\"prev\":null}";
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":1,\"op\":\"create_log\"," + log + ",\"body\":" + shaped + "}"))
              .error() == DecodeError::BAD_FIELD);
    const std::string fraction = "{\"header\":{\"facilitator\":\"Sam Example\",\"date\":\"2026-02-03\",\"unit\":null,"
                                 "\"location\":null,\"version\":null},\"created\":1.5,\"prev\":null}";
    CHECK(decode_record(with_crc("{\"v\":1,\"seq\":1,\"op\":\"create_log\"," + log + ",\"body\":" + fraction + "}"))
              .error() == DecodeError::MALFORMED);
}

TEST_CASE("A unit with an environment is stored as digits, a dash and the environment; one without is unchanged",
          "[journal][environment]")
{
    // A log with no environment writes the same bytes as before environments existed: the digits alone.
    const std::string plain = encoded(sample_record(1, CreateLogBody{sample_header(), sample_time(0), std::nullopt}));
    CHECK(plain.find("\"unit\":\"27\"") != std::string::npos);

    HeaderValues with_environment = sample_header();
    with_environment.m_UnitNumber = UnitNumber::parse("27", "prod").value();
    const std::string line = encoded(sample_record(1, CreateLogBody{with_environment, sample_time(0), std::nullopt}));
    CHECK(line.find("\"unit\":\"27-prod\"") != std::string::npos);

    const auto decoded = decode_record(line);
    REQUIRE(decoded.has_value());
    const auto& header = std::get<CreateLogBody>(decoded->m_Body).m_Header;
    CHECK(header.m_UnitNumber->digits() == "27");
    CHECK(header.m_UnitNumber->environment() == "prod");
    CHECK(encoded(*decoded) == line);

    // The line a build wrote before environments existed decodes to a unit without one.
    const auto old = decode_record(plain);
    REQUIRE(old.has_value());
    const auto& old_header = std::get<CreateLogBody>(old->m_Body).m_Header;
    CHECK(old_header.m_UnitNumber == UnitNumber::parse("27").value());
    CHECK(old_header.m_UnitNumber->environment().empty());

    // A change entry carries the stored text.
    const auto change = decode_record(
        encoded(sample_record(5, AddEntryBody{sample_change(5, 34, HeaderField::UNIT_NUMBER, "28-dev")})));
    REQUIRE(change.has_value());
    const auto& entry = std::get<AddEntryBody>(change->m_Body).m_Entry;
    CHECK(std::get<HeaderChangeEntry>(entry.m_Body).m_NewValue == "28-dev");
}

TEST_CASE("A journal record whose unit has a damaged environment is refused", "[journal][environment]")
{
    HeaderValues with_environment = sample_header();
    with_environment.m_UnitNumber = UnitNumber::parse("27", "prod").value();
    const std::string line = encoded(sample_record(1, CreateLogBody{with_environment, sample_time(0), std::nullopt}));
    // Rebuild the line with an environment that is not valid and the checksum of the new text.
    std::string object = line.substr(0, line.size() - 18) + "}";
    const auto at = object.find("27-prod");
    REQUIRE(at != std::string::npos);
    for (const char* bad : {"27-Prod", "27-", "27-pro d"})
    {
        INFO(bad);
        std::string damaged = object;
        damaged.replace(at, 7, bad);
        const auto decoded = decode_record(with_crc(damaged));
        REQUIRE_FALSE(decoded.has_value());
        CHECK(decoded.error() == DecodeError::BAD_FIELD);
    }
}
