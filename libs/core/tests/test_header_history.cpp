// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/header_history.hpp"
#include "golden_scenarios.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>

using namespace ej;
using ej::test::test_id;

namespace
{

UtcMillis minute(int m)
{
    return std::chrono::sys_days{std::chrono::year{2026} / 11 / 7} + std::chrono::minutes(m);
}

Timestamp at(int m)
{
    return Timestamp{minute(m), "Australia/Melbourne"};
}

Log make_log()
{
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    header.m_UnitNumber = UnitNumber::parse("27").value();
    header.m_Location = LocationValue{test_id<LocationId>(1), {}};
    header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2").value();
    return Log(std::move(header));
}

Log make_empty_header_log()
{
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    return Log(std::move(header));
}

std::string version_at(const Log& log, int m)
{
    const auto version = HeaderHistory(log).at(minute(m)).m_SoftwareVersion;
    return version ? version->format() : std::string{};
}

EntryId id(std::uint64_t n)
{
    return test_id<EntryId>(n);
}

// The "old" and "new" sides of a change line, as the export will print them.
std::string line_of(const Log& log, EntryId change)
{
    const Entry* entry = log.find(change);
    const auto old_value = HeaderHistory(log).previous_value(*entry);
    return old_value.value_or("") + " -> " + std::get<HeaderChangeEntry>(entry->m_Body).m_NewValue;
}

}  // namespace

TEST_CASE("Entries before a change see the initial header, after it the new value", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    CHECK(version_at(log, 59) == "1.4.2");
    CHECK(version_at(log, 61) == "1.5.0");
    CHECK(HeaderHistory(log).start().m_SoftwareVersion->format() == "1.4.2");
}

TEST_CASE("A second change on a field stacks, and different fields are independent", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    REQUIRE(change_header(log, HeaderField::UNIT_NUMBER, "31", at(70), id(2)).has_value());
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.6.0", at(80), id(3)).has_value());
    REQUIRE(change_header(log, HeaderField::LOCATION, "Back Room", at(90), id(4)).has_value());

    const HeaderHistory history(log);
    CHECK(history.at(minute(65)).m_SoftwareVersion->format() == "1.5.0");
    CHECK(history.at(minute(65)).m_UnitNumber->digits() == "27");
    CHECK(history.at(minute(75)).m_UnitNumber->digits() == "31");
    CHECK(history.at(minute(85)).m_SoftwareVersion->format() == "1.6.0");
    CHECK(history.at(minute(85)).m_Location->m_Id == test_id<LocationId>(1));
    CHECK(history.at(minute(95)).m_Location->m_Text == "Back Room");
    CHECK(history.previous_value(*log.find(id(3))) == "1.5.0");
    CHECK(history.previous_value(*log.find(id(2))) == "27");
    CHECK(history.previous_value(*log.find(id(4))) == test_id<LocationId>(1).format());
}

TEST_CASE("A change applies to an entry at exactly the same millisecond", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    CHECK(version_at(log, 60) == "1.5.0");
    CHECK(HeaderHistory(log).at(minute(60) - std::chrono::milliseconds(1)).m_SoftwareVersion->format() == "1.4.2");
}

TEST_CASE("Correct with no changes rewrites the initial value and adds no entry", "[header-history]")
{
    Log log = make_log();
    REQUIRE(correct_header(log, HeaderField::SOFTWARE_VERSION, "1.4.3").has_value());
    CHECK(log.entries().empty());
    CHECK(log.initial_header().m_SoftwareVersion->format() == "1.4.3");
    REQUIRE(correct_header(log, HeaderField::UNIT_NUMBER, "28").has_value());
    CHECK(log.initial_header().m_UnitNumber->digits() == "28");
    // an empty text clears an initial value, so the field can go back to missing
    REQUIRE(correct_header(log, HeaderField::LOCATION, "").has_value());
    CHECK_FALSE(log.initial_header().m_Location.has_value());
    REQUIRE(correct_header(log, HeaderField::LOCATION, "Back Room").has_value());
    CHECK(log.initial_header().m_Location->m_Text == "Back Room");
}

TEST_CASE("A change then a correction: the line follows, the header block does not", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    REQUIRE(correct_header(log, HeaderField::SOFTWARE_VERSION, "1.5.1").has_value());
    CHECK(line_of(log, id(1)) == "1.4.2 -> 1.5.1");
    CHECK(HeaderHistory(log).start().m_SoftwareVersion->format() == "1.4.2");
    CHECK(log.entries().size() == 1);
    CHECK_FALSE(log.entries()[0].m_EditedUtc.has_value());
}

TEST_CASE("A correction then a change: the old side and the header block follow", "[header-history]")
{
    Log log = make_log();
    REQUIRE(correct_header(log, HeaderField::SOFTWARE_VERSION, "1.4.3").has_value());
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    CHECK(line_of(log, id(1)) == "1.4.3 -> 1.5.0");
    CHECK(HeaderHistory(log).start().m_SoftwareVersion->format() == "1.4.3");
}

TEST_CASE("Correcting a change entry flows into the next change's old side", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.6.0", at(80), id(2)).has_value());
    REQUIRE(correct_header_change(log, id(1), "1.5.5").has_value());
    CHECK(line_of(log, id(1)) == "1.4.2 -> 1.5.5");
    CHECK(line_of(log, id(2)) == "1.5.5 -> 1.6.0");
    CHECK(version_at(log, 70) == "1.5.5");
    // correct_header fixes the latest live change
    REQUIRE(correct_header(log, HeaderField::SOFTWARE_VERSION, "1.6.1").has_value());
    CHECK(line_of(log, id(2)) == "1.5.5 -> 1.6.1");
}

TEST_CASE("Soft-deleting a change reverts the field from that time, restoring brings it back", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.6.0", at(80), id(2)).has_value());

    REQUIRE(log.set_deleted(id(1), true).has_value());
    CHECK(version_at(log, 70) == "1.4.2");
    CHECK(version_at(log, 90) == "1.6.0");
    CHECK(line_of(log, id(2)) == "1.4.2 -> 1.6.0");
    CHECK(line_of(log, id(1)) == "1.4.2 -> 1.5.0");  // a deleted line still has an old side

    // correct_header skips deleted changes: it fixes the latest live one
    REQUIRE(correct_header(log, HeaderField::SOFTWARE_VERSION, "1.6.2").has_value());
    CHECK(std::get<HeaderChangeEntry>(log.find(id(1))->m_Body).m_NewValue == "1.5.0");
    CHECK(std::get<HeaderChangeEntry>(log.find(id(2))->m_Body).m_NewValue == "1.6.2");

    REQUIRE(log.set_deleted(id(1), false).has_value());
    CHECK(version_at(log, 70) == "1.5.0");
    CHECK(line_of(log, id(2)) == "1.5.0 -> 1.6.2");
}

TEST_CASE("A change equal to the value in force is rejected", "[header-history]")
{
    Log log = make_log();
    const auto same = change_header(log, HeaderField::SOFTWARE_VERSION, "1.4.2", at(60), id(1));
    REQUIRE_FALSE(same.has_value());
    CHECK(same.error().m_Kind == HeaderError::Kind::UNCHANGED);
    CHECK(change_header(log, HeaderField::UNIT_NUMBER, "27", at(60), id(1)).error().m_Kind
          == HeaderError::Kind::UNCHANGED);
    CHECK(change_header(log, HeaderField::LOCATION, test_id<LocationId>(1).format(), at(60), id(1)).error().m_Kind
          == HeaderError::Kind::UNCHANGED);
    CHECK(log.entries().empty());

    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    CHECK(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(70), id(2)).error().m_Kind
          == HeaderError::Kind::UNCHANGED);
    // the same value is a real change at a time before the first change
    CHECK(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(30), id(3)).has_value());
}

TEST_CASE("An invalid value is rejected with the component's own error", "[header-history]")
{
    Log log = make_log();
    const auto version = change_header(log, HeaderField::SOFTWARE_VERSION, "1.5", at(60), id(1));
    REQUIRE_FALSE(version.has_value());
    CHECK(version.error().m_Kind == HeaderError::Kind::INVALID_SOFTWARE_VERSION);
    CHECK(version.error().m_SoftwareVersion == SoftwareVersion::Error::WRONG_COMPONENT_COUNT);

    const auto unit = change_header(log, HeaderField::UNIT_NUMBER, "027", at(60), id(1));
    REQUIRE_FALSE(unit.has_value());
    CHECK(unit.error().m_Kind == HeaderError::Kind::INVALID_UNIT_NUMBER);
    CHECK(unit.error().m_UnitNumber == UnitNumber::Error::LEADING_ZERO);

    CHECK(change_header(log, HeaderField::UNIT_NUMBER, "", at(60), id(1)).error().m_UnitNumber
          == UnitNumber::Error::EMPTY);
    CHECK(change_header(log, HeaderField::LOCATION, "", at(60), id(1)).error().m_Kind
          == HeaderError::Kind::EMPTY_LOCATION);
    CHECK(correct_header(log, HeaderField::SOFTWARE_VERSION, "x").error().m_Kind
          == HeaderError::Kind::INVALID_SOFTWARE_VERSION);
    CHECK(log.entries().empty());
    CHECK(log.initial_header().m_SoftwareVersion->format() == "1.4.2");
}

TEST_CASE("A duplicate entry id is refused", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    CHECK(change_header(log, HeaderField::SOFTWARE_VERSION, "1.6.0", at(70), id(1)).error().m_Kind
          == HeaderError::Kind::DUPLICATE_ID);
}

TEST_CASE("A change edited earlier than a prior change reorders the history", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.6.0", at(80), id(2)).has_value());

    // the review screen moves the second change to before the first
    Entry edit = *log.find(id(2));
    edit.m_Captured = at(40);
    REQUIRE(log.replace(id(2), edit, minute(200)).has_value());

    CHECK(version_at(log, 50) == "1.6.0");
    CHECK(version_at(log, 70) == "1.5.0");
    CHECK(line_of(log, id(2)) == "1.4.2 -> 1.6.0");
    CHECK(line_of(log, id(1)) == "1.6.0 -> 1.5.0");
}

TEST_CASE("A first change from an empty field has an empty old side", "[header-history]")
{
    Log log = make_empty_header_log();
    CHECK(HeaderHistory(log).at(minute(0)).m_UnitNumber == std::nullopt);
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.4.2", at(10), id(1)).has_value());
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(20), id(2)).has_value());
    CHECK_FALSE(HeaderHistory(log).previous_value(*log.find(id(1))).has_value());
    CHECK(line_of(log, id(1)) == " -> 1.4.2");
    CHECK(line_of(log, id(2)) == "1.4.2 -> 1.5.0");
}

TEST_CASE("Correcting something that is not a header change", "[header-history]")
{
    Log log = make_log();
    Entry note;
    note.m_Id = id(5);
    note.m_Captured = at(5);
    note.m_Body = NoteEntry{std::nullopt, "text"};
    REQUIRE(log.add(note).has_value());
    CHECK(correct_header_change(log, id(5), "1.5.0").error().m_Kind == HeaderError::Kind::NOT_A_HEADER_CHANGE);
    CHECK(correct_header_change(log, id(6), "1.5.0").error().m_Kind == HeaderError::Kind::UNKNOWN_ENTRY);
    CHECK_FALSE(HeaderHistory(log).previous_value(*log.find(id(5))).has_value());
}

TEST_CASE("A change cannot be corrected to the value before it", "[header-history]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::SOFTWARE_VERSION, "1.5.0", at(60), id(1)).has_value());
    CHECK(correct_header(log, HeaderField::SOFTWARE_VERSION, "1.4.2").error().m_Kind == HeaderError::Kind::UNCHANGED);
    CHECK(correct_header_change(log, id(1), "1.4.2").error().m_Kind == HeaderError::Kind::UNCHANGED);
    CHECK(line_of(log, id(1)) == "1.4.2 -> 1.5.0");
}

TEST_CASE("A unit may change its environment or lose it; the number can stay", "[header-history][environment]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::UNIT_NUMBER, "27-prod", at(60), id(1)).has_value());
    REQUIRE(change_header(log, HeaderField::UNIT_NUMBER, "27-dev", at(70), id(2)).has_value());
    REQUIRE(change_header(log, HeaderField::UNIT_NUMBER, "27", at(80), id(3)).has_value());

    const HeaderHistory history(log);
    CHECK(history.at(minute(59)).m_UnitNumber->environment().empty());
    CHECK(history.at(minute(65)).m_UnitNumber->environment() == "prod");
    CHECK(history.at(minute(75)).m_UnitNumber->environment() == "dev");
    CHECK(history.at(minute(85)).m_UnitNumber->environment().empty());
    CHECK(history.at(minute(85)).m_UnitNumber->digits() == "27");
    // The old side of a line is the stored text, so the export can print the unit as it was.
    CHECK(history.previous_value(*log.find(id(1))) == "27");
    CHECK(history.previous_value(*log.find(id(2))) == "27-prod");
    CHECK(history.previous_value(*log.find(id(3))) == "27-dev");
    CHECK(line_of(log, id(2)) == "27-prod -> 27-dev");

    // Only the stored text of the log is kept, in the stored form of the unit.
    CHECK(std::get<HeaderChangeEntry>(log.find(id(1))->m_Body).m_NewValue == "27-prod");
}

TEST_CASE("A change to the same unit with the same environment is refused, and a bad environment is refused",
          "[header-history][environment]")
{
    Log log = make_log();
    REQUIRE(change_header(log, HeaderField::UNIT_NUMBER, "27-prod", at(60), id(1)).has_value());
    const auto same = change_header(log, HeaderField::UNIT_NUMBER, "27-prod", at(70), id(2));
    REQUIRE_FALSE(same.has_value());
    CHECK(same.error().m_Kind == HeaderError::Kind::UNCHANGED);

    for (const char* text : {"27-Prod", "27-", "27-pro d", "27--prod"})
    {
        INFO(text);
        const auto refused = change_header(log, HeaderField::UNIT_NUMBER, text, at(70), id(3));
        REQUIRE_FALSE(refused.has_value());
        CHECK(refused.error().m_Kind == HeaderError::Kind::INVALID_UNIT_NUMBER);
        CHECK(refused.error().m_UnitNumber == UnitNumber::Error::BAD_ENVIRONMENT);
    }
    CHECK(log.entries().size() == 1);
}

TEST_CASE("Correct fixes the environment of the unit the log started with", "[header-history][environment]")
{
    Log log = make_log();
    REQUIRE(correct_header(log, HeaderField::UNIT_NUMBER, "27-prod").has_value());
    CHECK(log.entries().empty());
    CHECK(log.initial_header().m_UnitNumber->environment() == "prod");
    CHECK(header_field_text(log.initial_header(), HeaderField::UNIT_NUMBER) == "27-prod");
    REQUIRE(correct_header(log, HeaderField::UNIT_NUMBER, "27").has_value());
    CHECK(log.initial_header().m_UnitNumber->environment().empty());
    CHECK(header_field_text(log.initial_header(), HeaderField::UNIT_NUMBER) == "27");
}
