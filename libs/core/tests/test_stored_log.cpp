// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/stored_log.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 100;

StoredLog new_log()
{
    auto log = make_stored_log(sample_id<LogId>(LOG), CreateLogBody{sample_header(), sample_time(0), std::nullopt});
    REQUIRE(log.has_value());
    return std::move(*log);
}

Result<void, StorageError> apply(StoredLog& log, JournalBody body, std::uint64_t seq = 2)
{
    return apply_record(log, sample_record(seq, std::move(body), LOG));
}

void require_ok(const Result<void, StorageError>& result)
{
    const std::string text = result ? std::string() : describe(result.error());
    INFO(text);
    REQUIRE(result.has_value());
}

StorageErrorKind kind_of(const Result<void, StorageError>& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.error().m_Kind;
}

}  // namespace

TEST_CASE("A new log holds its header and nothing else", "[stored-log]")
{
    const StoredLog log = new_log();
    CHECK(log.m_Meta.m_Id == sample_id<LogId>(LOG));
    CHECK(log.m_Meta.m_CreatedUtc == sample_time(0));
    CHECK_FALSE(log.m_Meta.m_Archived);
    CHECK_FALSE(log.m_Meta.m_LastSharedUtc.has_value());
    CHECK(log.m_Log.entries().empty());
    CHECK(log.m_Log.initial_header().m_Tester == "Sam Example");
}

TEST_CASE("A header that breaks a limit does not make a log", "[stored-log]")
{
    HeaderValues header = sample_header();
    header.m_Tester = std::string(MAX_HEADER_TEXT_CODE_POINTS + 1, 'x');
    CHECK(make_stored_log(sample_id<LogId>(1), CreateLogBody{header, sample_time(0), std::nullopt}).error().m_Kind ==
          StorageErrorKind::LIMIT_EXCEEDED);

    header = sample_header();
    header.m_Tester = "broken \xC3";
    CHECK(make_stored_log(sample_id<LogId>(1), CreateLogBody{header, sample_time(0), std::nullopt}).error().m_Kind ==
          StorageErrorKind::INVALID_ARGUMENT);

    header = sample_header();
    header.m_Location = LocationValue{std::nullopt, ""};
    CHECK(make_stored_log(sample_id<LogId>(1), CreateLogBody{header, sample_time(0), std::nullopt}).error().m_Kind ==
          StorageErrorKind::INVALID_ARGUMENT);

    header = sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 2 / 30;
    CHECK_FALSE(make_stored_log(sample_id<LogId>(1), CreateLogBody{header, sample_time(0), std::nullopt}).has_value());
}

TEST_CASE("An error entry keeps the snapshot label of its type", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10, "Wrong Response")}));
    REQUIRE(log.m_Log.entries().size() == 1);
    CHECK(std::get<ErrorEntry>(log.m_Log.find(sample_id<EntryId>(1))->m_Body).m_ErrorTypeLabel == "Wrong Response");
}

TEST_CASE("An error entry needs a label within the limit", "[stored-log]")
{
    StoredLog log = new_log();
    CHECK(kind_of(apply(log, AddEntryBody{sample_error(1, 10, "")})) == StorageErrorKind::INVALID_ARGUMENT);

    CHECK(kind_of(apply(log, AddEntryBody{sample_error(3, 10, std::string(MAX_LABEL_CODE_POINTS + 1, 'x'))})) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(log.m_Log.entries().empty());
}

TEST_CASE("Notes are capped at 10,000 characters, counted as characters", "[stored-log]")
{
    StoredLog log = new_log();
    std::string exactly;
    for (std::size_t i = 0; i < MAX_NOTE_CODE_POINTS; ++i)
    {
        exactly += "\xC3\xA9";  // two bytes each
    }
    require_ok(apply(log, AddEntryBody{sample_note(1, 10, exactly)}));
    CHECK(kind_of(apply(log, AddEntryBody{sample_note(2, 10, exactly + "x")})) == StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(kind_of(apply(log, AddEntryBody{sample_error(3, 10, "Wrong Response", exactly + "x")})) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(kind_of(apply(log, AddEntryBody{sample_note(4, 10, "bad \xFF")})) == StorageErrorKind::INVALID_ARGUMENT);
    CHECK(log.m_Log.entries().size() == 1);
}

TEST_CASE("The number of entries in a log is bounded", "[stored-log]")
{
    StoredLog log = new_log();
    for (std::size_t i = 1; i <= MAX_ENTRIES_PER_LOG; ++i)
    {
        REQUIRE(apply(log, AddEntryBody{sample_error(i, static_cast<std::int64_t>(i % 1000))}).has_value());
    }
    CHECK(kind_of(apply(log, AddEntryBody{sample_error(MAX_ENTRIES_PER_LOG + 1, 5)})) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(log.m_Log.entries().size() == MAX_ENTRIES_PER_LOG);
}

TEST_CASE("An entry id is used once", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10)}));
    CHECK(kind_of(apply(log, AddEntryBody{sample_note(1, 11, "same id")})) == StorageErrorKind::ALREADY_EXISTS);
    CHECK(log.m_Log.entries().size() == 1);
}

TEST_CASE("Entries keep journal order and equal times sort by it", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(5, 10)}));
    require_ok(apply(log, AddEntryBody{sample_error(3, 10)}));  // the same minute
    require_ok(apply(log, AddEntryBody{sample_error(9, 5)}));   // earlier
    const auto sorted = log.m_Log.sorted_entries();
    REQUIRE(sorted.size() == 3);
    CHECK(sorted[0]->m_Id == sample_id<EntryId>(9));
    CHECK(sorted[1]->m_Id == sample_id<EntryId>(5));
    CHECK(sorted[2]->m_Id == sample_id<EntryId>(3));
}

TEST_CASE("A clock that stepped back puts the later entry first and nothing else changes", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 60)}));
    require_ok(apply(log, AddEntryBody{sample_error(2, 30)}));  // captured after, but the clock read earlier
    const auto sorted = log.m_Log.sorted_entries();
    REQUIRE(sorted.size() == 2);
    CHECK(sorted[0]->m_Id == sample_id<EntryId>(2));
    CHECK(sorted[1]->m_Id == sample_id<EntryId>(1));
    CHECK(log.m_Log.entries()[0].m_Id == sample_id<EntryId>(1));  // insertion order is journal order
}

TEST_CASE("Dates in 1970 and in the far future are stored as given", "[stored-log]")
{
    StoredLog log = new_log();
    Entry old = sample_error(1, 0);
    old.m_Captured.m_Utc = UtcMillis{};
    require_ok(apply(log, AddEntryBody{old}));
    Entry future = sample_error(2, 0);
    future.m_Captured.m_Utc = std::chrono::sys_days{std::chrono::year{9000} / 1 / 1};
    require_ok(apply(log, AddEntryBody{future}));
    CHECK(log.m_Log.find(sample_id<EntryId>(1))->m_Captured.m_Utc == UtcMillis{});
    CHECK(log.m_Log.find(sample_id<EntryId>(2))->m_Captured.m_Utc == future.m_Captured.m_Utc);
}

TEST_CASE("A header change is validated and must differ from the value in force", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_change(1, 10, HeaderField::UNIT_NUMBER, "28")}));
    CHECK(kind_of(apply(log, AddEntryBody{sample_change(2, 11, HeaderField::UNIT_NUMBER, "28")})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, AddEntryBody{sample_change(3, 12, HeaderField::UNIT_NUMBER, "x9")})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, AddEntryBody{sample_change(4, 12, HeaderField::SOFTWARE_VERSION, "1.4")})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    require_ok(apply(log, AddEntryBody{sample_change(5, 13, HeaderField::SOFTWARE_VERSION, "1.4.3")}));
    CHECK(log.m_Log.entries().size() == 2);
}

TEST_CASE("A new header change cannot arrive deleted or edited", "[stored-log]")
{
    StoredLog log = new_log();
    Entry deleted = sample_change(1, 10, HeaderField::UNIT_NUMBER, "28");
    deleted.m_Deleted = true;
    CHECK(kind_of(apply(log, AddEntryBody{deleted})) == StorageErrorKind::INVALID_ARGUMENT);
}

TEST_CASE("Edit replaces the content, keeps the id and the deleted flag, and records the edit time", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10, "Wrong Response")}));
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));

    Entry changed = sample_note(1, 20, "now a note");
    require_ok(apply(log, EditEntryBody{changed, sample_time(99)}));

    const Entry* entry = log.m_Log.find(sample_id<EntryId>(1));
    REQUIRE(entry != nullptr);
    CHECK(entry->m_Captured.m_Utc == sample_time(20));
    CHECK(entry->m_EditedUtc == sample_time(99));
    CHECK(entry->m_Deleted);
    CHECK(std::holds_alternative<NoteEntry>(entry->m_Body));

    require_ok(apply(log, EditEntryBody{sample_error(1, 21, "Other"), sample_time(100)}));
    CHECK(std::get<ErrorEntry>(log.m_Log.find(sample_id<EntryId>(1))->m_Body).m_ErrorTypeLabel == "Other");
}

TEST_CASE("Edit of an unknown entry is refused and changes nothing", "[stored-log]")
{
    StoredLog log = new_log();
    CHECK(kind_of(apply(log, EditEntryBody{sample_error(1, 10), sample_time(5)})) == StorageErrorKind::NOT_FOUND);
    CHECK(log.m_Log.entries().empty());
}

TEST_CASE("Edit of a header change validates its new value", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_change(1, 10, HeaderField::UNIT_NUMBER, "28")}));
    CHECK(kind_of(apply(log, EditEntryBody{sample_change(1, 10, HeaderField::UNIT_NUMBER, "abc"), sample_time(5)})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    require_ok(apply(log, EditEntryBody{sample_change(1, 10, HeaderField::UNIT_NUMBER, "30"), sample_time(5)}));
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.find(sample_id<EntryId>(1))->m_Body).m_NewValue == "30");
}

TEST_CASE("Soft delete and restore flip the flag; an unknown entry is refused", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10)}));
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));
    CHECK(log.m_Log.find(sample_id<EntryId>(1))->m_Deleted);
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), false}));
    CHECK_FALSE(log.m_Log.find(sample_id<EntryId>(1))->m_Deleted);
    CHECK(kind_of(apply(log, SetDeletedBody{sample_id<EntryId>(2), true})) == StorageErrorKind::NOT_FOUND);
}

TEST_CASE("Correct fixes the header without writing an entry", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "31"}));
    CHECK(log.m_Log.initial_header().m_UnitNumber->digits() == "31");
    CHECK(log.m_Log.entries().empty());

    CHECK(kind_of(apply(log, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "x"})) == StorageErrorKind::INVALID_ARGUMENT);
    require_ok(apply(log, CorrectHeaderBody{HeaderField::SOFTWARE_VERSION, ""}));
    CHECK_FALSE(log.m_Log.initial_header().m_SoftwareVersion.has_value());
}

TEST_CASE("Correct flows into the latest change of the field when there is one", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_change(1, 10, HeaderField::UNIT_NUMBER, "28")}));
    require_ok(apply(log, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "29"}));
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.find(sample_id<EntryId>(1))->m_Body).m_NewValue == "29");
    CHECK(log.m_Log.initial_header().m_UnitNumber->digits() == "27");  // the start is untouched
}

TEST_CASE("Mark shared records the time, also on an archived log", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, MarkSharedBody{sample_time(50)}));
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(50));
    require_ok(apply(log, ArchiveBody{}));
    require_ok(apply(log, MarkSharedBody{sample_time(60)}));
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(60));
}

TEST_CASE("An archived log stays editable (DAY-04) but is archived once", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10)}));
    require_ok(apply(log, ArchiveBody{}));
    CHECK(log.m_Meta.m_Archived);
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));
    require_ok(apply(log, EditEntryBody{sample_error(1, 12), sample_time(70)}));
    CHECK(kind_of(apply(log, ArchiveBody{})) == StorageErrorKind::NOT_OPEN_LOG);
}

TEST_CASE("A record of another log, a second create and a void change nothing", "[stored-log]")
{
    StoredLog log = new_log();
    CHECK(kind_of(apply_record(log, sample_record(2, ArchiveBody{}, LOG + 1))) == StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, CreateLogBody{sample_header(), sample_time(0), std::nullopt})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    require_ok(apply(log, VoidBody{2}));
    CHECK_FALSE(log.m_Meta.m_Archived);
}

TEST_CASE("A refused record leaves the log exactly as it was", "[stored-log]")
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10)}));
    require_ok(apply(log, AddEntryBody{sample_note(2, 11, "kept")}));
    const std::size_t before = log.m_Log.entries().size();

    CHECK_FALSE(apply(log, AddEntryBody{sample_error(1, 10)}).has_value());
    CHECK_FALSE(
        apply(log, EditEntryBody{sample_note(2, 11, std::string(MAX_NOTE_CODE_POINTS + 1, 'x')), sample_time(5)})
            .has_value());
    CHECK_FALSE(apply(log, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "no"}).has_value());

    CHECK(log.m_Log.entries().size() == before);
    CHECK(std::get<NoteEntry>(log.m_Log.find(sample_id<EntryId>(2))->m_Body).m_Value == "kept");
    CHECK(log.m_Log.initial_header().m_UnitNumber->digits() == "27");
}

TEST_CASE("Times that a journal cannot carry are refused", "[stored-log]")
{
    const UtcMillis huge{std::chrono::milliseconds{std::int64_t{1} << 60}};
    StoredLog log = new_log();

    Entry captured = sample_error(1, 0);
    captured.m_Captured.m_Utc = huge;
    CHECK(kind_of(apply(log, AddEntryBody{captured})) == StorageErrorKind::INVALID_ARGUMENT);

    Entry edited = sample_error(2, 0);
    edited.m_EditedUtc = huge;
    CHECK(kind_of(apply(log, AddEntryBody{edited})) == StorageErrorKind::INVALID_ARGUMENT);

    require_ok(apply(log, AddEntryBody{sample_error(3, 0)}));
    CHECK(kind_of(apply(log, EditEntryBody{sample_error(3, 5), huge})) == StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, EditEntryBody{captured, sample_time(1)})) == StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, MarkSharedBody{huge})) == StorageErrorKind::INVALID_ARGUMENT);

    CHECK(make_stored_log(sample_id<LogId>(1), CreateLogBody{sample_header(), huge, std::nullopt}).error().m_Kind ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(log.m_Log.entries().size() == 1);
}

TEST_CASE("A time zone name that is too long or not text is refused", "[stored-log]")
{
    StoredLog log = new_log();
    Entry long_zone = sample_error(1, 0);
    long_zone.m_Captured.m_Zone = std::string(MAX_ZONE_BYTES + 1, 'x');
    CHECK(kind_of(apply(log, AddEntryBody{long_zone})) == StorageErrorKind::INVALID_ARGUMENT);

    Entry bad_zone = sample_error(2, 0);
    bad_zone.m_Captured.m_Zone = "Australia/\xFF";
    CHECK(kind_of(apply(log, AddEntryBody{bad_zone})) == StorageErrorKind::INVALID_ARGUMENT);

    Entry no_zone = sample_error(3, 0);
    no_zone.m_Captured.m_Zone.clear();  // allowed: the zone is only a record of where the device was
    require_ok(apply(log, AddEntryBody{no_zone}));
}

TEST_CASE("A header value that is too long, empty or unchanged is refused", "[stored-log]")
{
    StoredLog log = new_log();
    CHECK(kind_of(apply(log,
                        CorrectHeaderBody{HeaderField::LOCATION, std::string(MAX_HEADER_TEXT_CODE_POINTS + 1, 'x')})) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    CHECK(kind_of(apply(log, AddEntryBody{sample_change(1, 5, HeaderField::LOCATION, "")})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, AddEntryBody{sample_change(2, 5, HeaderField::LOCATION, "North Site")})) ==
          StorageErrorKind::INVALID_ARGUMENT);
    CHECK(kind_of(apply(log, AddEntryBody{sample_change(3, 5, HeaderField::SOFTWARE_VERSION,
                                                        std::string(MAX_HEADER_TEXT_CODE_POINTS + 1, '1'))})) ==
          StorageErrorKind::LIMIT_EXCEEDED);
    require_ok(apply(log, AddEntryBody{sample_change(4, 5, HeaderField::LOCATION, "South Site")}));
    CHECK(kind_of(apply(log, EditEntryBody{sample_change(4, 5, HeaderField::LOCATION, ""), sample_time(9)})) ==
          StorageErrorKind::INVALID_ARGUMENT);
}

TEST_CASE("A location that names an id is stored by id", "[stored-log]")
{
    StoredLog log = new_log();
    const std::string id = sample_id<LocationId>(5).format();
    require_ok(apply(log, AddEntryBody{sample_change(1, 5, HeaderField::LOCATION, id)}));
    CHECK(std::get<HeaderChangeEntry>(log.m_Log.find(sample_id<EntryId>(1))->m_Body).m_NewValue == id);
}

// ---------------------------------------------------------------------------------------------------------------------
// The share rule (DAY-01, REV-05): a log is unshared when it has work that has not been shared.
// ---------------------------------------------------------------------------------------------------------------------

namespace
{

/// A log with two entries that has been shared.
StoredLog shared_log()
{
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 10)}));
    require_ok(apply(log, AddEntryBody{sample_note(2, 11, "kept")}));
    require_ok(apply(log, MarkSharedBody{sample_time(50)}));
    return log;
}

std::size_t live(const StoredLog& log)
{
    return log.m_Log.sorted_entries(false).size();
}

} // namespace

TEST_CASE("A log that was shared and not changed since reads as shared", "[stored-log][share]")
{
    const StoredLog log = shared_log();
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("A log with entries that was never shared is unshared, and an empty one is not", "[stored-log][share]")
{
    StoredLog log = new_log();
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
    require_ok(apply(log, AddEntryBody{sample_error(1, 10)}));
    CHECK(is_unshared(log.m_Meta, live(log)));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare); // before the first share the missing share time says it all
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));
    CHECK_FALSE(is_unshared(log.m_Meta, live(log))); // nothing is left to share
}

TEST_CASE("An entry added after a share makes the log unshared until the next share", "[stored-log][share]")
{
    StoredLog log = shared_log();
    require_ok(apply(log, AddEntryBody{sample_error(3, 20)}));
    CHECK(log.m_Meta.m_ChangedSinceShare);
    CHECK(is_unshared(log.m_Meta, live(log)));

    require_ok(apply(log, MarkSharedBody{sample_time(80)}));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
    CHECK(log.m_Meta.m_LastSharedUtc == sample_time(80));
}

TEST_CASE("An entry edited after a share makes the log unshared", "[stored-log][share]")
{
    StoredLog log = shared_log();
    require_ok(apply(log, EditEntryBody{sample_note(2, 11, "edited"), sample_time(60)}));
    CHECK(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("An entry deleted or restored after a share makes the log unshared", "[stored-log][share]")
{
    StoredLog log = shared_log();
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));
    CHECK(is_unshared(log.m_Meta, live(log)));
    CHECK(live(log) == 1);

    require_ok(apply(log, MarkSharedBody{sample_time(80)}));
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), false}));
    CHECK(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("Deleting what is deleted already, or restoring what is there, changes nothing to share",
          "[stored-log][share]")
{
    StoredLog log = shared_log();
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), false}));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);

    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));
    require_ok(apply(log, MarkSharedBody{sample_time(80)}));
    require_ok(apply(log, SetDeletedBody{sample_id<EntryId>(1), true}));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
}

TEST_CASE("A header change or a header correction that changes the text makes a shared log unshared",
          "[stored-log][share]")
{
    StoredLog changed = shared_log();
    require_ok(apply(changed, AddEntryBody{sample_change(3, 20, HeaderField::UNIT_NUMBER, "28")}));
    CHECK(is_unshared(changed.m_Meta, live(changed)));

    StoredLog corrected = shared_log();
    require_ok(apply(corrected, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "31"}));
    CHECK(is_unshared(corrected.m_Meta, live(corrected)));

    StoredLog corrected_change = shared_log();
    require_ok(apply(corrected_change, AddEntryBody{sample_change(3, 20, HeaderField::UNIT_NUMBER, "28")}));
    require_ok(apply(corrected_change, MarkSharedBody{sample_time(80)}));
    require_ok(apply(corrected_change, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "29"}));
    CHECK(is_unshared(corrected_change.m_Meta, live(corrected_change)));
}

TEST_CASE("A header correction to the value already there changes nothing to share", "[stored-log][share]")
{
    StoredLog log = shared_log();
    require_ok(apply(log, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "27"}));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("A refused record, a void and an archive leave a shared log shared", "[stored-log][share]")
{
    StoredLog log = shared_log();
    CHECK_FALSE(apply(log, AddEntryBody{sample_error(1, 10)}).has_value()); // an id that exists
    CHECK_FALSE(apply(log, CorrectHeaderBody{HeaderField::UNIT_NUMBER, "x"}).has_value());
    CHECK_FALSE(apply(log, SetDeletedBody{sample_id<EntryId>(99), true}).has_value());
    require_ok(apply(log, VoidBody{2}));
    require_ok(apply(log, ArchiveBody{}));
    CHECK_FALSE(log.m_Meta.m_ChangedSinceShare);
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("An archived log that is edited after its share is unshared again", "[stored-log][share]")
{
    StoredLog log = shared_log();
    require_ok(apply(log, ArchiveBody{}));
    require_ok(apply(log, EditEntryBody{sample_note(2, 11, "fixed in review"), sample_time(90)}));
    CHECK(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("Order decides, not time: a share is later than a change whatever the clocks say", "[stored-log][share]")
{
    // The second share is stamped earlier than the entry that came before it (a clock set back), and the entry that
    // came after it is stamped earlier than the share. The records are in order, and the order is what counts.
    StoredLog log = new_log();
    require_ok(apply(log, AddEntryBody{sample_error(1, 500)}));
    require_ok(apply(log, MarkSharedBody{sample_time(100)}));
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
    require_ok(apply(log, AddEntryBody{sample_error(2, 5)}));
    CHECK(is_unshared(log.m_Meta, live(log)));
    require_ok(apply(log, MarkSharedBody{sample_time(50)}));
    CHECK_FALSE(is_unshared(log.m_Meta, live(log)));
}

TEST_CASE("The digest of a log does not depend on the changed-since-share flag", "[stored-log][share]")
{
    StoredLog changed = shared_log();
    require_ok(apply(changed, AddEntryBody{sample_error(3, 20)}));
    StoredLog same = changed;
    same.m_Meta.m_ChangedSinceShare = false;
    CHECK(digest_of(changed) == digest_of(same));
}
