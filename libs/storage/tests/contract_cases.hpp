// SPDX-License-Identifier: GPL-3.0-or-later
//
// The contract of LogRepository. Every case runs against every fixture in `Fixtures`, so the in-memory repository
// cannot drift from the real one (docs/plan/stage-2.md, section 2.2).
//
// Include this file after defining, in the including file, the type list `Fixtures` and fixtures with
//     LogRepository& repo();  ScriptedClock& clock();
//     void fill_entries(LogId log, std::size_t count);   // adds `count` error entries, however quickly it can
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/interaction.hpp"
#include "error_journaling/log_repository.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>
#include <type_traits>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t FIRST_LOG = 1;

LogId log_id(std::uint64_t n)
{
    return sample_id<LogId>(n);
}

EntryId entry_id(std::uint64_t n)
{
    return sample_id<EntryId>(n);
}

template <typename Result>
void require_ok(const Result& result)
{
    const std::string text = result ? std::string() : describe(result.error());
    INFO(text);
    REQUIRE(result.has_value());
}

template <typename Result>
StorageErrorKind failure_of(const Result& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.error().m_Kind;
}

template <typename Fixture>
void start_log(Fixture& f, std::uint64_t n = FIRST_LOG)
{
    require_ok(f.repo().create_log(log_id(n), sample_header()));
}

std::vector<EntryId> time_order(const StoredLog& log, bool include_deleted = false)
{
    std::vector<EntryId> ids;
    for (const Entry* entry : log.m_Log.sorted_entries(include_deleted))
    {
        ids.push_back(entry->m_Id);
    }
    return ids;
}

// Nothing deletes a log without the confirmation type, and nobody outside the History path can make one (DS-05).
static_assert(!std::is_default_constructible_v<DeleteConfirmation>);
static_assert(!std::is_constructible_v<DeleteConfirmation, LogId>);
template <typename Repository>
concept DeletesWithoutConfirmation = requires(Repository& repository, LogId id) { repository.hard_delete_log(id); };
static_assert(!DeletesWithoutConfirmation<LogRepository>);
static_assert(!std::is_invocable_v<decltype(&LogRepository::hard_delete_log), LogRepository&, LogId, LogId>);

}  // namespace

TEMPLATE_LIST_TEST_CASE("A store with no log has no open log, no logs and no draft", "[contract][repository]", Fixtures)
{
    TestType f;
    const auto open = f.repo().open_log();
    REQUIRE(open.has_value());
    CHECK_FALSE(open->has_value());
    CHECK(f.repo().list_logs().value().empty());
    CHECK_FALSE(f.repo().get_draft().value().has_value());
    CHECK(failure_of(f.repo().load_log(log_id(1))) == StorageErrorKind::NOT_FOUND);
}

TEMPLATE_LIST_TEST_CASE("Creating a log makes it the open log with its header and creation time",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    f.clock().set(sample_time(5));
    start_log(f);

    const auto open = f.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Meta.m_Id == log_id(FIRST_LOG));
    CHECK(open->m_Meta.m_CreatedUtc == sample_time(5));
    CHECK_FALSE(open->m_Meta.m_Archived);
    CHECK(open->m_Log.initial_header().m_Facilitator == "Sam Example");
    CHECK(open->m_Log.initial_header().m_UnitNumber->digits() == "27");
    CHECK(open->m_Log.initial_header().m_Location->m_Text == "North Site");
    CHECK(open->m_Log.entries().empty());
}

TEMPLATE_LIST_TEST_CASE("Ids are unique and only one log is open", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    CHECK(failure_of(f.repo().create_log(log_id(FIRST_LOG), sample_header())) == StorageErrorKind::ALREADY_EXISTS);
    CHECK(failure_of(f.repo().create_log(log_id(2), sample_header())) == StorageErrorKind::OPEN_LOG_EXISTS);
    CHECK(f.repo().list_logs().value().size() == 1);
}

TEMPLATE_LIST_TEST_CASE("A header that breaks a rule does not create a log", "[contract][repository]", Fixtures)
{
    TestType f;
    HeaderValues header = sample_header();
    header.m_Facilitator = std::string(MAX_HEADER_TEXT_CODE_POINTS + 1, 'x');
    CHECK(failure_of(f.repo().create_log(log_id(1), header)) == StorageErrorKind::LIMIT_EXCEEDED);
    CHECK_FALSE(f.repo().open_log().value().has_value());
    CHECK(f.repo().list_logs().value().empty());
}

TEMPLATE_LIST_TEST_CASE("Added entries come back whole and in time order, equal times by arrival",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 30)));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_note(2, 10, "Played a song")));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(3, 30)));  // the same minute as 1
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(4, 20)));

    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(time_order(log) == std::vector<EntryId>{entry_id(2), entry_id(4), entry_id(1), entry_id(3)});
    REQUIRE(log.m_Log.entries().size() == 4);
    CHECK(log.m_Log.entries()[0].m_Id == entry_id(1));  // arrival order is the stored order
    CHECK(std::get<NoteEntry>(log.m_Log.find(entry_id(2))->m_Body).m_Value == "Played a song");
    CHECK(log.m_Log.find(entry_id(1))->m_Captured.m_Zone == "Australia/Melbourne");
}

TEMPLATE_LIST_TEST_CASE("An error entry keeps the label its type had when it was written", "[contract][repository]",
                        Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10, "Wrong Response", "robot paused")));
    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    const auto& error = std::get<ErrorEntry>(log.m_Log.find(entry_id(1))->m_Body);
    CHECK(error.m_ErrorTypeLabel == "Wrong Response");
    CHECK(error.m_ErrorType == sample_id<ErrorTypeId>(1));
    CHECK(error.m_AttachedNote == "robot paused");
}

TEMPLATE_LIST_TEST_CASE("An entry for an unknown log is refused", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    CHECK(failure_of(f.repo().add_entry(log_id(9), sample_error(1, 10))) == StorageErrorKind::NOT_FOUND);
    CHECK(failure_of(f.repo().set_entry_deleted(log_id(9), entry_id(1), true)) == StorageErrorKind::NOT_FOUND);
    CHECK(failure_of(f.repo().mark_shared(log_id(9), sample_time(1))) == StorageErrorKind::NOT_FOUND);
    CHECK(failure_of(f.repo().archive_log(log_id(9))) == StorageErrorKind::NOT_FOUND);
}

TEMPLATE_LIST_TEST_CASE("A rejected write leaves the log unchanged", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    const StoredLog before = f.repo().load_log(log_id(FIRST_LOG)).value();

    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 11))) == StorageErrorKind::ALREADY_EXISTS);
    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG), sample_error(2, 11, ""))) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(failure_of(
              f.repo().add_entry(log_id(FIRST_LOG), sample_note(3, 11, std::string(MAX_NOTE_CODE_POINTS + 1, 'x')))) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(failure_of(f.repo().edit_entry(log_id(FIRST_LOG), sample_error(7, 11), sample_time(1))) ==
          StorageErrorKind::NOT_FOUND);
    CHECK(failure_of(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(7), true)) == StorageErrorKind::NOT_FOUND);
    CHECK(failure_of(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "not a number")) ==
          StorageErrorKind::INVALID_ARGUMENT);

    const StoredLog after = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(after.m_Log.entries().size() == before.m_Log.entries().size());
    CHECK(after.m_Log.initial_header().m_UnitNumber->digits() == "27");
    CHECK(time_order(after) == time_order(before));
}

TEMPLATE_LIST_TEST_CASE("The entries of a log are bounded", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    f.fill_entries(log_id(FIRST_LOG), MAX_ENTRIES_PER_LOG - 1);
    require_ok(
        f.repo().add_entry(log_id(FIRST_LOG), sample_error(MAX_ENTRIES_PER_LOG + 1000, 1)));  // the last that fits
    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG), sample_error(MAX_ENTRIES_PER_LOG + 1001, 1))) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.entries().size() == MAX_ENTRIES_PER_LOG);
}

TEMPLATE_LIST_TEST_CASE("Text of every kind comes back exactly as it went in", "[contract][repository]", Fixtures)
{
    TestType f;
    HeaderValues header = sample_header();
    header.m_Facilitator = "Sam \"Example\" caf\xC3\xA9 \xF0\x9F\x98\x80";
    require_ok(f.repo().create_log(log_id(FIRST_LOG), header));

    const std::string nasty = std::string("line one\nline two\ttab \"quotes\" back\\slash NUL:") +
                              std::string(1, '\0') + " end \xE2\x86\x92 \xF0\x9F\x98\x80 '; DROP TABLE entry; --";
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_note(1, 10, nasty)));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(2, 11, "Wrong \xE2\x86\x92 Response", nasty)));

    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_Facilitator == header.m_Facilitator);
    CHECK(std::get<NoteEntry>(log.m_Log.find(entry_id(1))->m_Body).m_Value == nasty);
    const auto& error = std::get<ErrorEntry>(log.m_Log.find(entry_id(2))->m_Body);
    CHECK(error.m_AttachedNote == nasty);
    CHECK(error.m_ErrorTypeLabel == "Wrong \xE2\x86\x92 Response");
}

TEMPLATE_LIST_TEST_CASE("A location by id, a note template and a header change all keep their ids",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    HeaderValues header = sample_header();
    header.m_Location = LocationValue{sample_id<LocationId>(5), "North Site"};
    require_ok(f.repo().create_log(log_id(FIRST_LOG), header));

    Entry templated = sample_note(1, 10, "Sam Example");
    std::get<NoteEntry>(templated.m_Body).m_Template = sample_id<NoteTemplateId>(9);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), templated));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG),
                                  sample_change(2, 20, HeaderField::LOCATION, sample_id<LocationId>(6).format())));

    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_Location->m_Id == sample_id<LocationId>(5));
    CHECK(log.m_Log.initial_header().m_Location->m_Text == "North Site");
    CHECK(std::get<NoteEntry>(log.m_Log.find(entry_id(1))->m_Body).m_Template == sample_id<NoteTemplateId>(9));
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.find(entry_id(2))->m_Body).m_NewValue ==
          sample_id<LocationId>(6).format());
}

TEMPLATE_LIST_TEST_CASE("A log with no unit, location or version keeps them empty", "[contract][repository]", Fixtures)
{
    TestType f;
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 8;
    require_ok(f.repo().create_log(log_id(FIRST_LOG), header));
    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK_FALSE(log.m_Log.initial_header().m_UnitNumber.has_value());
    CHECK_FALSE(log.m_Log.initial_header().m_Location.has_value());
    CHECK_FALSE(log.m_Log.initial_header().m_SoftwareVersion.has_value());
    CHECK(log.m_Log.initial_header().m_LocalDate == std::chrono::year{2026} / 11 / 8);
}

TEMPLATE_LIST_TEST_CASE("The environment of a unit is kept, changed by a correction and read back",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    HeaderValues header = sample_header();
    header.m_UnitNumber = UnitNumber::parse("27", "prod").value();
    require_ok(f.repo().create_log(log_id(FIRST_LOG), header));
    CHECK(f.repo().open_log().value()->m_Log.initial_header().m_UnitNumber->stored() == "27-prod");
    CHECK(f.repo().list_logs().value().at(0).m_Header.m_UnitNumber->environment() == "prod");

    // A correction with no change entry fixes the unit the log started with: the number, the environment, or both.
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "31"));
    StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_UnitNumber->digits() == "31");
    CHECK(log.m_Log.initial_header().m_UnitNumber->environment().empty());
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "32-dev"));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_UnitNumber->stored() == "32-dev");

    // A value that is not a unit is refused, and the unit stays as it was.
    CHECK(failure_of(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "32-Dev")) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.initial_header().m_UnitNumber->stored() == "32-dev");

    // A change entry carries the new unit in its stored form.
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_change(1, 10, HeaderField::UNIT_NUMBER, "32-qa")));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.find(entry_id(1))->m_Body).m_NewValue == "32-qa");
    CHECK(log.m_Log.initial_header().m_UnitNumber->stored() == "32-dev"); // the start did not move
}

TEMPLATE_LIST_TEST_CASE("The commit of a version is kept, changed by a correction and read back",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    HeaderValues header = sample_header();
    header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2", "A1B2C3D").value();
    require_ok(f.repo().create_log(log_id(FIRST_LOG), header));
    CHECK(f.repo().open_log().value()->m_Log.initial_header().m_SoftwareVersion->stored() == "1.4.2+a1b2c3d");
    CHECK(f.repo().list_logs().value().at(0).m_Header.m_SoftwareVersion->commit() == "a1b2c3d");

    // A correction with no change entry fixes the version the log started with: the number, the commit, or both.
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::SOFTWARE_VERSION, "1.4.3"));
    StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_SoftwareVersion->format() == "1.4.3");
    CHECK(log.m_Log.initial_header().m_SoftwareVersion->commit().empty());
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::SOFTWARE_VERSION, "1.4.3+0123456"));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_SoftwareVersion->stored() == "1.4.3+0123456");

    // A value that is not a version with a commit is refused, and the version stays as it was.
    CHECK(failure_of(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::SOFTWARE_VERSION, "1.4.3+xyz")) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.initial_header().m_SoftwareVersion->stored() ==
          "1.4.3+0123456");

    // A change entry carries the new version in its stored form.
    require_ok(
        f.repo().add_entry(log_id(FIRST_LOG), sample_change(1, 10, HeaderField::SOFTWARE_VERSION, "1.5.0+e4f5a6b")));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.find(entry_id(1))->m_Body).m_NewValue == "1.5.0+e4f5a6b");
    CHECK(log.m_Log.initial_header().m_SoftwareVersion->stored() == "1.4.3+0123456"); // the start did not move
}

TEMPLATE_LIST_TEST_CASE("Edit changes the content and records the edit time", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_error(1, 40, "Other", "added later"), sample_time(90)));

    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    const Entry* entry = log.m_Log.find(entry_id(1));
    REQUIRE(entry != nullptr);
    CHECK(entry->m_Captured.m_Utc == sample_time(40));
    CHECK(entry->m_EditedUtc == sample_time(90));
    CHECK(std::get<ErrorEntry>(entry->m_Body).m_ErrorTypeLabel == "Other");
    CHECK(std::get<ErrorEntry>(entry->m_Body).m_AttachedNote == "added later");
    CHECK(log.m_Log.entries().size() == 1);
}

TEMPLATE_LIST_TEST_CASE("Soft delete hides an entry from the timeline and restore brings it back",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(2, 20)));
    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(1), true));

    StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(time_order(log) == std::vector<EntryId>{entry_id(2)});
    CHECK(time_order(log, true) == std::vector<EntryId>{entry_id(1), entry_id(2)});
    CHECK(log.m_Log.entries().size() == 2);  // nothing was removed

    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(1), false));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(time_order(log) == std::vector<EntryId>{entry_id(1), entry_id(2)});
}

TEMPLATE_LIST_TEST_CASE("Correct changes the header and writes no entry; Change adds an entry",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "31"));
    StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_UnitNumber->digits() == "31");
    CHECK(log.m_Log.entries().empty());

    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_change(1, 30, HeaderField::SOFTWARE_VERSION, "1.4.3")));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_SoftwareVersion->format() == "1.4.2");  // the start did not move
    REQUIRE(log.m_Log.entries().size() == 1);
    const auto& change = std::get<HeaderChangeEntry>(log.m_Log.entries()[0].m_Body);
    CHECK(change.m_Field == HeaderField::SOFTWARE_VERSION);
    CHECK(change.m_NewValue == "1.4.3");

    // A second Correct now fixes the change, not the start.
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::SOFTWARE_VERSION, "1.4.4"));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.initial_header().m_SoftwareVersion->format() == "1.4.2");
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.entries()[0].m_Body).m_NewValue == "1.4.4");
}

TEMPLATE_LIST_TEST_CASE("A header change to the value in force is refused", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG), sample_change(1, 30, HeaderField::UNIT_NUMBER, "27"))) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.entries().empty());
}

TEMPLATE_LIST_TEST_CASE("Archiving keeps the log and leaves no open log", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().archive_log(log_id(FIRST_LOG)));

    CHECK_FALSE(f.repo().open_log().value().has_value());
    const auto logs = f.repo().list_logs().value();
    REQUIRE(logs.size() == 1);
    CHECK(logs[0].m_Archived);
    CHECK(logs[0].m_LiveEntryCount == 1);
    CHECK(logs[0].m_Header.m_Facilitator == "Sam Example");
    CHECK(logs[0].m_Header.m_UnitNumber->digits() == "27");
    CHECK(logs[0].m_TotalEntryCount == 1);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Meta.m_Archived);
    CHECK(failure_of(f.repo().archive_log(log_id(FIRST_LOG))) == StorageErrorKind::NOT_OPEN_LOG);

    // The next day's log can start now.
    require_ok(f.repo().create_log(log_id(2), sample_header()));
    CHECK(f.repo().open_log().value()->m_Meta.m_Id == log_id(2));
}

TEMPLATE_LIST_TEST_CASE("An archived log can still be edited, deleted from, corrected and shared (DAY-04)",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().archive_log(log_id(FIRST_LOG)));

    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_error(1, 12), sample_time(70)));
    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(1), true));
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "32"));
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(80)));

    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(log.m_Log.find(entry_id(1))->m_Deleted);
    CHECK(log.m_Log.initial_header().m_UnitNumber->digits() == "32");
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(80));
    CHECK_FALSE(f.repo().open_log().value().has_value());  // editing did not reopen it

    // Archiving is the boundary for new entries: only the open log accepts one.
    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG), sample_error(2, 20))) == StorageErrorKind::NOT_OPEN_LOG);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.entries().size() == 1);
}

TEMPLATE_LIST_TEST_CASE("Starting a new log archives the open one in a single step", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));

    HeaderValues next = sample_header();
    next.m_LocalDate = std::chrono::year{2026} / 11 / 8;
    require_ok(f.repo().start_new_log(log_id(FIRST_LOG), log_id(2), next));

    const auto open = f.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Meta.m_Id == log_id(2));
    CHECK(open->m_Meta.m_Previous == log_id(FIRST_LOG));
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Meta.m_Archived);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.entries().size() == 1);
}

TEMPLATE_LIST_TEST_CASE("A refused start leaves the old log open and creates nothing", "[contract][repository]",
                        Fixtures)
{
    TestType f;
    start_log(f);

    CHECK(failure_of(f.repo().start_new_log(log_id(7), log_id(2), sample_header())) == StorageErrorKind::NOT_OPEN_LOG);
    CHECK(failure_of(f.repo().start_new_log(log_id(FIRST_LOG), log_id(FIRST_LOG), sample_header())) ==
          StorageErrorKind::ALREADY_EXISTS);
    HeaderValues bad = sample_header();
    bad.m_Facilitator = std::string(MAX_HEADER_TEXT_CODE_POINTS + 1, 'x');
    CHECK(failure_of(f.repo().start_new_log(log_id(FIRST_LOG), log_id(2), bad)) == StorageErrorKind::LIMIT_EXCEEDED);

    const auto open = f.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Meta.m_Id == log_id(FIRST_LOG));
    CHECK_FALSE(open->m_Meta.m_Archived);
    CHECK(f.repo().list_logs().value().size() == 1);
}

TEMPLATE_LIST_TEST_CASE("Logs are listed newest first by date, then by creation time", "[contract][repository]",
                        Fixtures)
{
    TestType f;
    const auto day = [](unsigned d)
    {
        HeaderValues header = sample_header();
        header.m_LocalDate = std::chrono::year{2026} / 11 / std::chrono::day{d};
        return header;
    };
    f.clock().set(sample_time(1));
    require_ok(f.repo().create_log(log_id(1), day(7)));
    require_ok(f.repo().archive_log(log_id(1)));
    f.clock().set(sample_time(2));
    require_ok(f.repo().create_log(log_id(2), day(9)));
    require_ok(f.repo().archive_log(log_id(2)));
    f.clock().set(sample_time(3));
    require_ok(f.repo().create_log(log_id(3), day(9)));  // a second log on the same date
    require_ok(f.repo().archive_log(log_id(3)));
    f.clock().set(sample_time(4));
    require_ok(f.repo().create_log(log_id(4), day(8)));

    const auto logs = f.repo().list_logs().value();
    REQUIRE(logs.size() == 4);
    CHECK(logs[0].m_Id == log_id(3));
    CHECK(logs[1].m_Id == log_id(2));
    CHECK(logs[2].m_Id == log_id(4));
    CHECK(logs[3].m_Id == log_id(1));
}

TEMPLATE_LIST_TEST_CASE("The creation time comes from the clock, even when it stepped back or reads 1970",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    f.clock().set(UtcMillis{});
    start_log(f, 1);
    CHECK(f.repo().load_log(log_id(1)).value().m_Meta.m_CreatedUtc == UtcMillis{});
    require_ok(f.repo().archive_log(log_id(1)));
    f.clock().set(sample_time(100));
    start_log(f, 2);
    require_ok(f.repo().archive_log(log_id(2)));
    f.clock().set(sample_time(50));  // stepped back
    start_log(f, 3);
    CHECK(f.repo().load_log(log_id(3)).value().m_Meta.m_CreatedUtc == sample_time(50));
}

TEMPLATE_LIST_TEST_CASE("Mark shared records the time", "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    CHECK_FALSE(f.repo().load_log(log_id(FIRST_LOG)).value().m_Meta.m_LastSharedUtc.has_value());
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(77)));
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Meta.m_LastSharedUtc == sample_time(77));
    CHECK(f.repo().list_logs().value()[0].m_LastSharedUtc == sample_time(77));
}

namespace
{

/// Whether the repository says the log has work that has not been shared, from the log and from the list of logs.
template <typename Fixture>
void require_unshared(Fixture& f, bool expected, std::uint64_t n = FIRST_LOG)
{
    const StoredLog log = f.repo().load_log(log_id(n)).value();
    CHECK(is_unshared(log.m_Meta, log.m_Log.sorted_entries(false).size()) == expected);
    for (const LogSummary& summary : f.repo().list_logs().value())
    {
        if (summary.m_Id == log_id(n))
        {
            CHECK(is_unshared(summary) == expected);
        }
    }
}

} // namespace

TEMPLATE_LIST_TEST_CASE("A log is unshared until it is shared and again after every change (DAY-01, REV-05)",
                        "[contract][repository][share]", Fixtures)
{
    TestType f;
    start_log(f);
    require_unshared(f, false); // nothing to share yet

    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(2, 11)));
    require_unshared(f, true); // never shared

    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(50)));
    require_unshared(f, false); // share, no change: shared

    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(3, 12)));
    require_unshared(f, true); // share, add: unshared
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(60)));
    require_unshared(f, false);

    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_error(1, 13), sample_time(65)));
    require_unshared(f, true); // share, edit: unshared
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(70)));
    require_unshared(f, false);

    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(2), true));
    require_unshared(f, true); // share, delete: unshared
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(80)));
    require_unshared(f, false);

    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(2), false));
    require_unshared(f, true); // restore: unshared
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(85)));

    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "31"));
    require_unshared(f, true); // a header that reads differently
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(90)));
    require_ok(f.repo().correct_header(log_id(FIRST_LOG), HeaderField::UNIT_NUMBER, "31"));
    require_unshared(f, false); // the same text again: nothing new to send
}

TEMPLATE_LIST_TEST_CASE("A refused write, an archive and a log with nothing left do not make a shared log unshared",
                        "[contract][repository][share]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(50)));

    CHECK_FALSE(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)).has_value()); // an id that exists
    CHECK_FALSE(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(9), true).has_value());
    require_unshared(f, false);

    require_ok(f.repo().archive_log(log_id(FIRST_LOG)));
    require_unshared(f, false);

    // Past logs stay editable (DAY-04): a change in Review brings the log back to unshared.
    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_error(1, 12), sample_time(70)));
    require_unshared(f, true);

    // Deleting everything leaves nothing to share, so there is nothing to warn about.
    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(1), true));
    require_unshared(f, false);
}

TEMPLATE_LIST_TEST_CASE("Each log is unshared or shared on its own account", "[contract][repository][share]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    require_ok(f.repo().mark_shared(log_id(FIRST_LOG), sample_time(50)));
    require_ok(f.repo().start_new_log(log_id(FIRST_LOG), log_id(2), sample_header()));
    require_ok(f.repo().add_entry(log_id(2), sample_error(2, 20)));

    require_unshared(f, false, FIRST_LOG); // archived, shared, and unchanged since
    require_unshared(f, true, 2);          // never shared
    require_ok(f.repo().mark_shared(log_id(2), sample_time(60)));
    require_ok(f.repo().add_entry(log_id(2), sample_error(3, 21)));
    require_unshared(f, false, FIRST_LOG);
    require_unshared(f, true, 2);
}

TEMPLATE_LIST_TEST_CASE("Only an archived log can be deleted, and only with its own confirmation",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));

    // The open log is never deleted (DS-05).
    CHECK(failure_of(
              f.repo().hard_delete_log(log_id(FIRST_LOG), HistoryDeleteConfirmation::confirmed(log_id(FIRST_LOG)))) ==
          StorageErrorKind::LOG_NOT_ARCHIVED);
    CHECK(f.repo().open_log().value().has_value());

    require_ok(f.repo().archive_log(log_id(FIRST_LOG)));
    // A confirmation for another log does not delete this one.
    CHECK(failure_of(f.repo().hard_delete_log(log_id(FIRST_LOG), HistoryDeleteConfirmation::confirmed(log_id(2)))) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).has_value());

    require_ok(f.repo().hard_delete_log(log_id(FIRST_LOG), HistoryDeleteConfirmation::confirmed(log_id(FIRST_LOG))));
    CHECK(failure_of(f.repo().load_log(log_id(FIRST_LOG))) == StorageErrorKind::NOT_FOUND);
    CHECK(f.repo().list_logs().value().empty());
    CHECK(failure_of(f.repo().hard_delete_log(log_id(FIRST_LOG), HistoryDeleteConfirmation::confirmed(log_id(
                                                                     FIRST_LOG)))) == StorageErrorKind::NOT_FOUND);
}

TEMPLATE_LIST_TEST_CASE("The draft is set, replaced, cleared by an empty string and bounded", "[contract][repository]",
                        Fixtures)
{
    TestType f;
    require_ok(f.repo().set_draft("half a thought", sample_time(1)));
    auto draft = f.repo().get_draft().value();
    REQUIRE(draft.has_value());
    CHECK(draft->m_Text == "half a thought");
    CHECK(draft->m_UpdatedUtc == sample_time(1));

    require_ok(f.repo().set_draft("a whole thought", sample_time(2)));
    CHECK(f.repo().get_draft().value()->m_Text == "a whole thought");

    require_ok(f.repo().set_draft("", sample_time(3)));
    CHECK_FALSE(f.repo().get_draft().value().has_value());

    CHECK(failure_of(f.repo().set_draft(std::string(MAX_NOTE_CODE_POINTS + 1, 'x'), sample_time(4))) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(failure_of(f.repo().set_draft("broken \xC3", sample_time(4))) == StorageErrorKind::INVALID_ARGUMENT);
    CHECK_FALSE(f.repo().get_draft().value().has_value());
}

TEMPLATE_LIST_TEST_CASE("Adding a note clears the draft in the same step; other entries and refused notes keep it",
                        "[contract][repository]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().set_draft("typed note", sample_time(1)));

    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 10)));
    CHECK(f.repo().get_draft().value().has_value());

    // A refused note must not clear it.
    CHECK_FALSE(f.repo().add_entry(log_id(FIRST_LOG), sample_note(1, 11, "duplicate id")).has_value());
    CHECK(f.repo().get_draft().value().has_value());

    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_note(2, 12, "typed note")));
    CHECK_FALSE(f.repo().get_draft().value().has_value());
    CHECK(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.entries().size() == 2);
}

// --- interactions (Stage 14): the memory and the SQLite repository keep them the same way (T-I2-3)
// ---------------------

namespace
{

PersonId person_id(std::uint64_t n)
{
    return sample_id<PersonId>(n);
}

} // namespace

TEMPLATE_LIST_TEST_CASE("An interaction entry comes back whole, with its people in the order they were picked",
                        "[contract][repository][interaction]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_error(1, 30)));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG),
                                  sample_interaction_entry(2, 20, {person_id(7), person_id(3), person_id(9)},
                                                           "Morning Medley", "asked about the schedule")));
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(3, 10)));

    const StoredLog log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(time_order(log) == std::vector<EntryId>{entry_id(3), entry_id(2), entry_id(1)});
    const auto& named = std::get<InteractionEntry>(log.m_Log.find(entry_id(2))->m_Body);
    CHECK(named.m_Type == sample_id<InteractionTypeId>(1));
    CHECK(named.m_TypeLabel == "Spoke with");
    CHECK(named.m_People == std::vector<PersonId>{person_id(7), person_id(3), person_id(9)});
    CHECK(named.m_Value == "Morning Medley");
    CHECK(named.m_AttachedNote == "asked about the schedule");
    const auto& nobody = std::get<InteractionEntry>(log.m_Log.find(entry_id(3))->m_Body);
    CHECK(nobody.m_People.empty());
    CHECK_FALSE(nobody.m_Value.has_value());
    CHECK_FALSE(nobody.m_AttachedNote.has_value());
    CHECK(f.repo().list_logs().value().front().m_TotalEntryCount == 3);
}

TEMPLATE_LIST_TEST_CASE("Editing an interaction sets, reorders and clears its people and keeps the entry in place",
                        "[contract][repository][interaction]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10))); // logged first, named second
    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(5), person_id(4)}),
                                   sample_time(50)));
    auto log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People ==
          std::vector<PersonId>{person_id(5), person_id(4)});
    CHECK(log.m_Log.find(entry_id(1))->m_EditedUtc == sample_time(50));

    require_ok(f.repo().edit_entry(
        log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(4), person_id(5)}, "x"), sample_time(51)));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People ==
          std::vector<PersonId>{person_id(4), person_id(5)});
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_Value == "x");

    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10), sample_time(52)));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People.empty());
    CHECK(log.m_Log.entries().size() == 1);
}

TEMPLATE_LIST_TEST_CASE("An interaction can be changed into another kind of entry and back",
                        "[contract][repository][interaction]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(5)})));
    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_note(1, 10, "now a note"), sample_time(20)));
    auto log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::holds_alternative<NoteEntry>(log.m_Log.find(entry_id(1))->m_Body));
    require_ok(
        f.repo().edit_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(6)}), sample_time(21)));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People ==
          std::vector<PersonId>{person_id(6)});
}

TEMPLATE_LIST_TEST_CASE("A deleted interaction is hidden and restored with its people",
                        "[contract][repository][interaction]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(5), person_id(6)})));
    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(1), true));
    auto log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(time_order(log).empty());
    CHECK(time_order(log, true) == std::vector<EntryId>{entry_id(1)});
    CHECK(f.repo().list_logs().value().front().m_LiveEntryCount == 0);

    require_ok(f.repo().set_entry_deleted(log_id(FIRST_LOG), entry_id(1), false));
    log = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(std::get<InteractionEntry>(log.m_Log.find(entry_id(1))->m_Body).m_People ==
          std::vector<PersonId>{person_id(5), person_id(6)});
}

TEMPLATE_LIST_TEST_CASE("An interaction that breaks a rule is refused and leaves the log as it was",
                        "[contract][repository][interaction]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(5)})));
    const auto before = f.repo().load_log(log_id(FIRST_LOG)).value();

    std::vector<PersonId> twenty_one;
    for (std::uint64_t n = 1; n <= MAX_PEOPLE_PER_INTERACTION + 1; ++n)
    {
        twenty_one.push_back(person_id(n));
    }
    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(2, 11, twenty_one))) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(failure_of(
              f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(2, 11, {person_id(5), person_id(5)}))) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(failure_of(f.repo().add_entry(log_id(FIRST_LOG),
                                        sample_interaction_entry(2, 11, {}, std::nullopt, std::nullopt, ""))) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(failure_of(f.repo().edit_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, twenty_one),
                                         sample_time(50))) == StorageErrorKind::LIMIT_EXCEEDED);

    const auto after = f.repo().load_log(log_id(FIRST_LOG)).value();
    CHECK(digest_of(after) == digest_of(before));

    // Twenty is the most there can be.
    twenty_one.pop_back();
    require_ok(f.repo().edit_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, twenty_one), sample_time(51)));
    CHECK(std::get<InteractionEntry>(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.find(entry_id(1))->m_Body)
              .m_People.size() == MAX_PEOPLE_PER_INTERACTION);
}

TEMPLATE_LIST_TEST_CASE("An archived log keeps its interactions and a new log starts without them",
                        "[contract][repository][interaction]", Fixtures)
{
    TestType f;
    start_log(f);
    require_ok(f.repo().add_entry(log_id(FIRST_LOG), sample_interaction_entry(1, 10, {person_id(5)})));
    require_ok(f.repo().start_new_log(log_id(FIRST_LOG), log_id(2), sample_header()));
    CHECK(std::get<InteractionEntry>(f.repo().load_log(log_id(FIRST_LOG)).value().m_Log.find(entry_id(1))->m_Body)
              .m_People == std::vector<PersonId>{person_id(5)});
    CHECK(f.repo().open_log().value()->m_Log.entries().empty());
}
