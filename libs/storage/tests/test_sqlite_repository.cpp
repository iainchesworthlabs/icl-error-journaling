// SPDX-License-Identifier: GPL-3.0-or-later
//
// What is particular to the SQLite repository: the schema and the settings, the write path of section 2.6 (W1 to W7),
// and what a file that is not ours, is newer, or is not a database does to the open.
#include "error_journaling/clock.hpp"
#include "error_journaling/sqlite_log_repository.hpp"
#include "raw_db.hpp"
#include "recording_journal.hpp"
#include "storage_samples.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using Call = RecordingJournal::Call;

namespace
{

constexpr std::uint64_t LOG = 1;

LogId log_id(std::uint64_t n)
{
    return sample_id<LogId>(n);
}

EntryId entry_id(std::uint64_t n)
{
    return sample_id<EntryId>(n);
}

struct Store
{
    TempDir m_Dir;
    ScriptedClock m_Clock{sample_time(0)};
    RecordingJournal m_Journal;
    std::unique_ptr<SqliteLogRepository> m_Repository;

    [[nodiscard]] std::filesystem::path database() const { return m_Dir.path() / "ej.db"; }

    Result<void, StorageError> open(SqliteLogRepository::Options options = {})
    {
        auto opened = SqliteLogRepository::open(database(), m_Clock, m_Journal, std::move(options));
        if (!opened)
        {
            return std::unexpected(opened.error());
        }
        m_Repository = std::move(*opened);
        return {};
    }

    Store() { REQUIRE(open().has_value()); }

    SqliteLogRepository& repo() { return *m_Repository; }

    void start() { REQUIRE(repo().create_log(log_id(LOG), sample_header()).has_value()); }
};

QVariant scalar(const Store& store, const char* sql)
{
    RawDb raw(store.database());
    return raw.scalar(QString::fromLatin1(sql));
}

StorageError error_of(const Result<void, StorageError>& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.error();
}

}  // namespace

TEST_CASE("A new database has the schema, the identity and the settings of version 1", "[sqlite]")
{
    Store store;
    RawDb raw(store.database());
    REQUIRE(raw.is_open());
    CHECK(raw.scalar(QStringLiteral("PRAGMA application_id")).toInt() == SQLITE_APPLICATION_ID);
    CHECK(raw.scalar(QStringLiteral("PRAGMA user_version")).toInt() == SQLITE_SCHEMA_VERSION);
    CHECK(raw.scalar(QStringLiteral("PRAGMA journal_mode")).toString() == "wal");

    std::set<QString> tables;
    for (const char* name :
         {"day_log", "entry", "error_entry", "note_entry", "header_change_entry", "draft", "store_state"})
    {
        CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'table' AND name = ?"),
                         {QString::fromLatin1(name)})
                  .toInt() == 1);
    }
    for (const char* name : {"day_log_archived", "day_log_one_open", "entry_by_time"})
    {
        CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM sqlite_master WHERE type = 'index' AND name = ?"),
                         {QString::fromLatin1(name)})
                  .toInt() == 1);
    }
    CHECK(raw.scalar(QStringLiteral("SELECT applied_seq FROM store_state")).toInt() == 0);
    CHECK(raw.scalar(QStringLiteral("SELECT journal_generation FROM store_state")).toInt() == 0);
}

TEST_CASE("The database allows one open log and does not let a log with entries be deleted by accident", "[sqlite]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());

    RawDb raw(store.database());
    // A second open log is refused by the database itself, not only by the repository.
    CHECK_FALSE(raw.exec(QStringLiteral("INSERT INTO day_log (id, local_date, facilitator, created_utc, archived) "
                                        "VALUES ('another', '2026-11-08', 'Sam Example', 0, 0)")));
    // With foreign keys on, deleting a log that has entries is restricted.
    REQUIRE(raw.exec(QStringLiteral("PRAGMA foreign_keys = ON")));
    CHECK_FALSE(raw.exec(QStringLiteral("DELETE FROM day_log")));
    CHECK_FALSE(raw.exec(QStringLiteral("DELETE FROM entry")));
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().size() == 1);
}

TEST_CASE("Everything survives closing and opening the database again", "[sqlite]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10, "Wrong Response", "kept")).has_value());
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(2, 20, HeaderField::UNIT_NUMBER, "28")).has_value());
    REQUIRE(store.repo().set_draft("not yet a note", sample_time(3)).has_value());
    REQUIRE(store.repo().mark_shared(log_id(LOG), sample_time(4)).has_value());

    store.m_Repository.reset();
    REQUIRE(store.open().has_value());

    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Meta.m_Id == log_id(LOG));
    CHECK(open->m_Meta.m_LastSharedUtc == sample_time(4));
    CHECK(open->m_Log.entries().size() == 2);
    CHECK(std::get<ErrorEntry>(open->m_Log.find(entry_id(1))->m_Body).m_AttachedNote == "kept");
    CHECK(store.repo().get_draft().value()->m_Text == "not yet a note");
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 4);  // create, two entries, the share
}

TEST_CASE("W3: the open fails when the database does not report write-ahead logging", "[sqlite]")
{
    TempDir dir;
    ScriptedClock clock;
    NullJournal journal;
    // An in-memory database answers "memory" to journal_mode=WAL: the setting was asked for and not given.
    const auto opened = SqliteLogRepository::open(":memory:", clock, journal);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind == StorageErrorKind::DATABASE);
    CHECK(opened.error().m_Message.find("journal_mode") != std::string::npos);
}

TEST_CASE("W1 and W2: the journal record is durable before the database commits", "[sqlite][write-path]")
{
    Store store;
    store.start();

    RawDb other(store.database());
    int visible_while_appending = -1;
    store.m_Journal.m_OnAppend = [&](const JournalRecord&)
    { visible_while_appending = other.scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt(); };
    store.m_Journal.m_Events.clear();

    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());

    CHECK(visible_while_appending == 0);  // the journal had the record, the database did not yet
    CHECK(other.scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt() == 1);  // and then it did
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::APPEND});
    REQUIRE(store.m_Journal.m_Records.size() == 2);  // the create_log of the fixture, then this one
    CHECK(journal_op(store.m_Journal.m_Records[0].m_Body) == JournalOp::CREATE_LOG);
    CHECK(journal_op(store.m_Journal.m_Records[1].m_Body) == JournalOp::ADD_ENTRY);
    CHECK(store.m_Journal.m_Records[1].m_Seq == 2);
}

TEST_CASE("The hook after the commit sees the committed entry", "[sqlite][write-path]")
{
    Store store;
    store.m_Repository.reset();
    RawDb* observer = nullptr;
    int seen_at_commit = -1;
    int seen_before_commit = -1;
    SqliteLogRepository::Options options;
    options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
    {
        if (observer != nullptr && point == CrashPoint::BEFORE_DB_COMMIT)
        {
            seen_before_commit = observer->scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt();
        }
        if (observer != nullptr && point == CrashPoint::AFTER_DB_COMMIT)
        {
            seen_at_commit = observer->scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt();
        }
        return {};
    };
    REQUIRE(store.open(options).has_value());
    store.start();
    RawDb raw(store.database());
    observer = &raw;
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    CHECK(seen_before_commit == 0);
    CHECK(seen_at_commit == 1);
}

TEST_CASE("W4: the applied sequence number advances with every commit", "[sqlite][write-path]")
{
    Store store;
    store.start();
    CHECK(store.repo().store_applied_seq().value() == 1);
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 1);
    CHECK(store.repo().journal_generation().value() == 1);

    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    CHECK(store.repo().store_applied_seq().value() == 2);
    REQUIRE(store.repo().edit_entry(log_id(LOG), sample_error(1, 11), sample_time(50)).has_value());
    CHECK(store.repo().store_applied_seq().value() == 3);
    REQUIRE(store.repo().set_entry_deleted(log_id(LOG), entry_id(1), true).has_value());
    CHECK(store.repo().store_applied_seq().value() == 4);
    REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::UNIT_NUMBER, "29").has_value());
    CHECK(store.repo().store_applied_seq().value() == 5);
    REQUIRE(store.repo().mark_shared(log_id(LOG), sample_time(60)).has_value());
    CHECK(store.repo().store_applied_seq().value() == 6);
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 6);

    // A refused write does not advance it.
    CHECK_FALSE(store.repo().add_entry(log_id(LOG), sample_error(1, 12)).has_value());
    CHECK(store.repo().store_applied_seq().value() == 6);

    // The draft is not journalled and does not touch it.
    REQUIRE(store.repo().set_draft("typing", sample_time(70)).has_value());
    CHECK(store.repo().store_applied_seq().value() == 6);
}

TEST_CASE("W4: each log has its own applied sequence number, and the store follows the open one",
          "[sqlite][write-path]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    REQUIRE(store.repo().archive_log(log_id(LOG)).has_value());
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 3);  // create, add, archive
    CHECK(store.repo().store_applied_seq().value() == 0);       // no log is open

    REQUIRE(store.repo().create_log(log_id(2), sample_header()).has_value());
    CHECK(store.repo().store_applied_seq().value() == 1);
    CHECK(store.repo().journal_generation().value() == 2);

    // Editing the archived log moves its own number and leaves the open log's alone.
    REQUIRE(store.repo().set_entry_deleted(log_id(LOG), entry_id(1), true).has_value());
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 4);
    CHECK(store.repo().store_applied_seq().value() == 1);
    CHECK(store.repo().applied_seq(log_id(9)).error().m_Kind == StorageErrorKind::NOT_FOUND);
}

TEST_CASE("W5: the note and the cleared draft commit together or not at all", "[sqlite][write-path]")
{
    Store store;
    store.m_Repository.reset();
    bool fail_commit = true;
    SqliteLogRepository::Options options;
    options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
    {
        if (fail_commit && point == CrashPoint::BEFORE_DB_COMMIT)
        {
            return std::unexpected(make_error(StorageErrorKind::DATABASE, "injected"));
        }
        return {};
    };
    REQUIRE(store.open(options).has_value());
    fail_commit = false;
    store.start();
    REQUIRE(store.repo().set_draft("typed note", sample_time(1)).has_value());

    fail_commit = true;
    CHECK_FALSE(store.repo().add_entry(log_id(LOG), sample_note(1, 10, "typed note")).has_value());
    CHECK(store.repo().get_draft().value().has_value());  // neither the entry nor the cleared draft
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().empty());

    fail_commit = false;
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_note(1, 10, "typed note")).has_value());
    CHECK_FALSE(store.repo().get_draft().value().has_value());
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().size() == 1);
}

TEST_CASE("W6: a commit that fails after the journal append cancels the record and leaves no entry",
          "[sqlite][write-path]")
{
    Store store;
    store.m_Repository.reset();
    bool fail_commit = false;
    SqliteLogRepository::Options options;
    options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
    {
        if (fail_commit && point == CrashPoint::BEFORE_DB_COMMIT)
        {
            return std::unexpected(make_error(StorageErrorKind::DATABASE, "injected commit failure"));
        }
        return {};
    };
    REQUIRE(store.open(options).has_value());
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());  // seq 2
    store.m_Journal.m_Events.clear();

    fail_commit = true;
    const auto failed =
        store.repo().add_entry(log_id(LOG), sample_error(2, 11));  // seq 3 is journalled, then cancelled
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error().m_Kind == StorageErrorKind::DATABASE);
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::APPEND, Call::CANCEL});
    CHECK(store.m_Journal.m_Cancelled == std::vector<std::uint64_t>{3});

    // The caller must not see an entry that is absent, and the store is usable afterwards.
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.find(entry_id(2)) == nullptr);
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 2);
    fail_commit = false;
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(2, 11)).has_value());
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 5);  // 3 was cancelled by 4, the retry is 5
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().size() == 2);
}

TEST_CASE("W6: a record that cannot be cancelled is reported in the error", "[sqlite][write-path]")
{
    Store store;
    store.m_Repository.reset();
    SqliteLogRepository::Options options;
    options.m_Hook = [](CrashPoint point) -> Result<void, StorageError>
    {
        if (point == CrashPoint::BEFORE_DB_COMMIT)
        {
            return std::unexpected(make_error(StorageErrorKind::DISK_FULL, "injected"));
        }
        return {};
    };
    store.m_Journal.m_FailCancel = make_error(StorageErrorKind::JOURNAL, "cannot write");
    REQUIRE(store.open(options).has_value());
    const auto failed = store.repo().create_log(log_id(LOG), sample_header());
    CHECK(error_of(failed).m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(error_of(failed).m_Message.find("could not be cancelled") != std::string::npos);
    CHECK_FALSE(store.repo().open_log().value().has_value());
}

TEST_CASE("W7: a full disk on the journal append or on the commit acknowledges nothing and leaves the store usable",
          "[sqlite][write-path]")
{
    Store store;
    store.start();

    store.m_Journal.m_FailAppend = make_error(StorageErrorKind::DISK_FULL, "no space for the journal");
    store.m_Journal.m_Events.clear();
    CHECK(error_of(store.repo().add_entry(log_id(LOG), sample_error(1, 10))).m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::APPEND});  // nothing to cancel: nothing was appended
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().empty());
    CHECK(store.repo().applied_seq(log_id(LOG)).value() == 1);

    store.m_Journal.m_FailAppend.reset();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().size() == 1);
}

TEST_CASE("A failed append of the first record of a pair cancels nothing and creates nothing", "[sqlite][write-path]")
{
    Store store;
    store.start();
    store.m_Journal.m_FailAppend = make_error(StorageErrorKind::JOURNAL, "cannot write");
    CHECK_FALSE(store.repo().start_new_log(log_id(LOG), log_id(2), sample_header()).has_value());
    CHECK(store.repo().open_log().value()->m_Meta.m_Id == log_id(LOG));
    CHECK(store.repo().list_logs().value().size() == 1);
}

TEST_CASE(
    "Start a new log: the new log is journalled first, the pair commits as one, and a failure leaves the old log open",
    "[sqlite][write-path]")
{
    Store store;
    store.m_Repository.reset();
    std::optional<CrashPoint> fail_at;
    SqliteLogRepository::Options options;
    options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
    {
        if (fail_at == point)
        {
            return std::unexpected(make_error(StorageErrorKind::DATABASE, "injected"));
        }
        return {};
    };
    REQUIRE(store.open(options).has_value());
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());

    // A failure between the two records, and one at the commit: the old log stays open, the new one does not exist, and
    // both records are cancelled, last first.
    for (const CrashPoint point : {CrashPoint::MID_ARCHIVE_PAIR, CrashPoint::BEFORE_DB_COMMIT})
    {
        fail_at = point;
        store.m_Journal.m_Events.clear();
        CHECK_FALSE(store.repo().start_new_log(log_id(LOG), log_id(2), sample_header()).has_value());
        CHECK(store.repo().open_log().value()->m_Meta.m_Id == log_id(LOG));
        CHECK(store.repo().list_logs().value().size() == 1);
        const auto calls = store.m_Journal.calls();
        if (point == CrashPoint::MID_ARCHIVE_PAIR)
        {
            CHECK(calls == std::vector<Call>{Call::APPEND, Call::CANCEL});
        }
        else
        {
            CHECK(calls == std::vector<Call>{Call::APPEND, Call::APPEND, Call::CANCEL, Call::CANCEL});
            CHECK(store.m_Journal.m_Events.back().m_Log == log_id(2));  // the new log's record, cancelled last
        }
    }

    fail_at.reset();
    store.m_Journal.m_Events.clear();
    store.m_Journal.m_Records.clear();
    REQUIRE(store.repo().start_new_log(log_id(LOG), log_id(2), sample_header()).has_value());
    REQUIRE(store.m_Journal.m_Records.size() == 2);
    CHECK(store.m_Journal.m_Records[0].m_Log == log_id(2));
    CHECK(journal_op(store.m_Journal.m_Records[0].m_Body) == JournalOp::CREATE_LOG);
    CHECK(std::get<CreateLogBody>(store.m_Journal.m_Records[0].m_Body).m_Previous == log_id(LOG));
    CHECK(store.m_Journal.m_Records[1].m_Log == log_id(LOG));
    CHECK(journal_op(store.m_Journal.m_Records[1].m_Body) == JournalOp::ARCHIVE);
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::APPEND, Call::APPEND, Call::ARCHIVE});
    CHECK(store.repo().open_log().value()->m_Meta.m_Id == log_id(2));
}

TEST_CASE("Archiving moves the journal after the commit and trims the log at a crash point", "[sqlite][write-path]")
{
    Store store;
    store.m_Repository.reset();
    std::vector<CrashPoint> seen;
    SqliteLogRepository::Options options;
    options.m_Hook = [&](CrashPoint point) -> Result<void, StorageError>
    {
        seen.push_back(point);
        return {};
    };
    REQUIRE(store.open(options).has_value());
    store.start();
    seen.clear();
    store.m_Journal.m_Events.clear();
    REQUIRE(store.repo().archive_log(log_id(LOG)).has_value());
    CHECK(seen == std::vector<CrashPoint>{CrashPoint::BEFORE_DB_COMMIT, CrashPoint::AFTER_DB_COMMIT,
                                          CrashPoint::MID_CHECKPOINT});
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::APPEND, Call::ARCHIVE});
}

TEST_CASE("A log is written to its journal even after it is archived", "[sqlite][write-path]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    REQUIRE(store.repo().archive_log(log_id(LOG)).has_value());
    store.m_Journal.m_Events.clear();

    // Edit, delete and share an archived log: each appends a record to its journal, as for the open log.
    REQUIRE(store.repo().edit_entry(log_id(LOG), sample_error(1, 12), sample_time(70)).has_value());
    REQUIRE(store.repo().set_entry_deleted(log_id(LOG), entry_id(1), true).has_value());
    REQUIRE(store.repo().mark_shared(log_id(LOG), sample_time(80)).has_value());
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::APPEND, Call::APPEND, Call::APPEND});
    CHECK(store.m_Journal.m_Records.back().m_Seq == 6);  // create, add, archive, then these three

    // A new entry is refused, and nothing is journalled for it.
    CHECK(error_of(store.repo().add_entry(log_id(LOG), sample_error(2, 20))).m_Kind == StorageErrorKind::NOT_OPEN_LOG);
    CHECK(store.m_Journal.m_Records.size() == 6);
}

TEST_CASE("A hard delete removes every row of the log and its journal, and keeps the log if the journal stays",
          "[sqlite]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_note(2, 11, "a note")).has_value());
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(3, 12, HeaderField::UNIT_NUMBER, "28")).has_value());
    REQUIRE(store.repo().archive_log(log_id(LOG)).has_value());
    REQUIRE(store.repo().create_log(log_id(2), sample_header()).has_value());
    REQUIRE(store.repo().add_entry(log_id(2), sample_error(10, 10)).has_value());

    // The journal refuses to go: the log is still there, whole.
    store.m_Journal.m_FailRemove = make_error(StorageErrorKind::JOURNAL, "cannot remove");
    CHECK_FALSE(
        store.repo().hard_delete_log(log_id(LOG), HistoryDeleteConfirmation::confirmed(log_id(LOG))).has_value());
    CHECK(store.repo().load_log(log_id(LOG)).value().m_Log.entries().size() == 3);

    store.m_Journal.m_FailRemove.reset();
    store.m_Journal.m_Events.clear();
    REQUIRE(store.repo().hard_delete_log(log_id(LOG), HistoryDeleteConfirmation::confirmed(log_id(LOG))).has_value());
    CHECK(store.m_Journal.calls() == std::vector<Call>{Call::REMOVE});

    RawDb raw(store.database());
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM day_log")).toInt() == 1);
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM entry")).toInt() == 1);  // the other log's
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM error_entry")).toInt() == 1);
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM note_entry")).toInt() == 0);
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM header_change_entry")).toInt() == 0);
    CHECK(store.repo().open_log().value()->m_Meta.m_Id == log_id(2));
}

TEST_CASE("A correction keeps the stored old side of later header changes in step", "[sqlite]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(1, 10, HeaderField::UNIT_NUMBER, "28")).has_value());
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(2, 20, HeaderField::UNIT_NUMBER, "29")).has_value());
    RawDb raw(store.database());
    CHECK(raw.scalar(QStringLiteral("SELECT old_value FROM header_change_entry WHERE new_value = '29'")).toString() ==
          "28");

    REQUIRE(store.repo()
                .correct_header(log_id(LOG), HeaderField::UNIT_NUMBER, "31")
                .has_value());  // fixes the later change
    CHECK(raw.scalar(QStringLiteral("SELECT new_value FROM header_change_entry WHERE old_value = '28'")).toString() ==
          "31");
    CHECK(raw.scalar(QStringLiteral("SELECT old_value FROM header_change_entry WHERE entry_id = ?"),
                     {QString::fromStdString(entry_id(1).format())})
              .toString() == "27");
}

TEST_CASE("A file that is not ours is refused and left exactly as it was (C14)", "[sqlite][open]")
{
    TempDir dir;
    const auto file = dir.path() / "ej.db";
    {
        RawDb raw(file);
        REQUIRE(raw.is_open());
        REQUIRE(raw.exec(QStringLiteral("CREATE TABLE notes (text TEXT)")));
        REQUIRE(raw.exec(QStringLiteral("INSERT INTO notes VALUES ('someone else')")));
    }
    SECTION("an application id of zero with tables in it") {}
    SECTION("another application id")
    {
        RawDb raw(file);
        REQUIRE(raw.exec(QStringLiteral("PRAGMA application_id = 12345")));
    }
    const auto before = snapshot_of(dir.path());

    ScriptedClock clock;
    NullJournal journal;
    const auto opened = SqliteLogRepository::open(file, clock, journal);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind == StorageErrorKind::NOT_OUR_DATABASE);
    CHECK(snapshot_of(dir.path()) == before);
}

TEST_CASE("A database written by a newer version is refused and left exactly as it was (C16)", "[sqlite][open]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    store.m_Repository.reset();
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version = 2")));
    }
    const auto before = snapshot_of(store.m_Dir.path()).at("ej.db");
    const auto opened = SqliteLogRepository::open(store.database(), store.m_Clock, store.m_Journal);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind == StorageErrorKind::NEWER_SCHEMA);
    // The database file is byte for byte as it was. (Reading a WAL database may leave its side files behind, the log
    // empty.)
    const auto after = snapshot_of(store.m_Dir.path());
    CHECK(after.at("ej.db") == before);
    if (const auto wal = after.find("ej.db-wal"); wal != after.end())
    {
        CHECK(wal->second.empty());
    }
}

TEST_CASE("Our application id with a schema version this build does not know is reported, not opened", "[sqlite][open]")
{
    Store store;
    store.m_Repository.reset();
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version = 0")));
    }
    const auto opened = SqliteLogRepository::open(store.database(), store.m_Clock, store.m_Journal);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind == StorageErrorKind::CORRUPT);
}

TEST_CASE("A file that is not a database is corruption and is left as it was", "[sqlite][open]")
{
    TempDir dir;
    const auto file = dir.path() / "ej.db";
    {
        std::ofstream out(file, std::ios::binary);
        out << std::string(8192, 'x');
    }
    const auto before = snapshot_of(dir.path());
    ScriptedClock clock;
    NullJournal journal;
    const auto opened = SqliteLogRepository::open(file, clock, journal);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind == StorageErrorKind::CORRUPT);
    CHECK(snapshot_of(dir.path()) == before);
}

TEST_CASE("A file that cannot be opened is a permission problem, not corruption", "[sqlite][open]")
{
    TempDir dir;
    ScriptedClock clock;
    NullJournal journal;
    // The folder does not exist.
    CHECK(SqliteLogRepository::open(dir.path() / "missing" / "ej.db", clock, journal).error().m_Kind ==
          StorageErrorKind::PERMISSION);
    // A folder stands where the file should be: depending on the system the open or the first read says no, but never
    // that the file is corrupt, and nothing is rebuilt.
    std::filesystem::create_directories(dir.path() / "ej.db");
    const auto opened = SqliteLogRepository::open(dir.path() / "ej.db", clock, journal);
    REQUIRE_FALSE(opened.has_value());
    CHECK(opened.error().m_Kind != StorageErrorKind::CORRUPT);
    CHECK(std::filesystem::is_directory(dir.path() / "ej.db"));
}

TEST_CASE("A database that another connection holds locked is reported as locked", "[sqlite][open]")
{
    Store store;
    store.m_Repository.reset();
    RawDb holder(store.database());
    REQUIRE(holder.exec(QStringLiteral("BEGIN EXCLUSIVE")));
    SqliteLogRepository::Options options;
    options.m_BusyTimeoutMs = 50;
    const auto opened = SqliteLogRepository::open(store.database(), store.m_Clock, store.m_Journal, options);
    // In WAL mode an exclusive writer blocks only other writers; either the open waits it out and works, or it reports
    // the lock. It must never call the file corrupt.
    if (!opened.has_value())
    {
        CHECK(opened.error().m_Kind == StorageErrorKind::LOCKED);
    }
    CHECK(holder.exec(QStringLiteral("ROLLBACK")));
}

TEST_CASE("A write that cannot get the lock reports it, and changes nothing", "[sqlite][open]")
{
    Store store;
    store.m_Repository.reset();
    SqliteLogRepository::Options options;
    options.m_BusyTimeoutMs = 50;
    REQUIRE(store.open(options).has_value());
    store.start();

    RawDb holder(store.database());
    REQUIRE(holder.exec(QStringLiteral("BEGIN IMMEDIATE")));
    const auto failed = store.repo().add_entry(log_id(LOG), sample_error(1, 10));
    CHECK(error_of(failed).m_Kind == StorageErrorKind::LOCKED);
    CHECK(store.m_Journal.m_Records.size() == 1);  // only the create_log: nothing was journalled for the refused write
    REQUIRE(holder.exec(QStringLiteral("ROLLBACK")));
    CHECK(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
}

TEST_CASE("The repository gives the database back: the file can be opened elsewhere after it is closed", "[sqlite]")
{
    Store store;
    store.start();
    store.m_Repository.reset();
    RawDb raw(store.database());
    CHECK(raw.scalar(QStringLiteral("SELECT count(*) FROM day_log")).toInt() == 1);
    CHECK(scalar(store, "SELECT count(*) FROM day_log").toInt() == 1);
}

// ---------------------------------------------------------------------------------------------------------------------
// A database that is damaged under the repository
// ---------------------------------------------------------------------------------------------------------------------

TEST_CASE("A stored value that is not valid is reported as corruption, not read as something else", "[sqlite][damage]")
{
    Store store;
    store.start();
    Entry templated = sample_note(2, 20, "a note");
    std::get<NoteEntry>(templated.m_Body).m_Template = sample_id<NoteTemplateId>(9);
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
    REQUIRE(store.repo().add_entry(log_id(LOG), templated).has_value());
    REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(3, 30, HeaderField::UNIT_NUMBER, "28")).has_value());
    REQUIRE(store.repo().start_new_log(log_id(LOG), log_id(2), sample_header()).has_value());
    store.m_Repository.reset();

    const std::string first = "'" + log_id(LOG).format() + "'";
    const std::string second = "'" + log_id(2).format() + "'";
    const std::vector<std::string> damage = {
        "UPDATE day_log SET local_date = '2026-13-45' WHERE id = " + first,
        "UPDATE day_log SET unit_number = 'zz' WHERE id = " + first,
        "UPDATE day_log SET software_version = '1.2' WHERE id = " + first,
        "UPDATE day_log SET location_id = 'nonsense' WHERE id = " + first,
        "UPDATE day_log SET previous_log_id = 'nonsense' WHERE id = " + second,
        "UPDATE entry SET id = 'nonsense' WHERE id = '" + entry_id(1).format() + "'",
        "UPDATE error_entry SET error_type_id = 'nonsense'",
        "UPDATE note_entry SET template_id = 'nonsense'",
        "UPDATE header_change_entry SET field = 'colour'",
    };
    for (const std::string& statement : damage)
    {
        INFO(statement);
        TempDir copy_dir;
        std::filesystem::copy_file(store.database(), copy_dir.path() / "ej.db");
        {
            RawDb raw(copy_dir.path() / "ej.db");
            REQUIRE(raw.exec(QString::fromStdString(statement)));
        }
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(copy_dir.path() / "ej.db", clock, journal);
        REQUIRE(repo.has_value());
        // Whichever call reads the damaged row says so; none of them hands back a log with the value invented.
        const auto listed = (*repo)->list_logs();
        const auto old_log = (*repo)->load_log(log_id(LOG));
        const auto new_log = (*repo)->load_log(log_id(2));
        const auto is_corrupt = [](const auto& result)
        { return !result.has_value() && result.error().m_Kind == StorageErrorKind::CORRUPT; };
        CHECK((is_corrupt(listed) || is_corrupt(old_log) || is_corrupt(new_log)));
    }
}

TEST_CASE("Two open logs in the database are reported, not chosen between", "[sqlite][damage]")
{
    Store store;
    store.start();
    REQUIRE(store.repo().archive_log(log_id(LOG)).has_value());
    REQUIRE(store.repo().create_log(log_id(2), sample_header()).has_value());
    store.m_Repository.reset();
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("DROP INDEX day_log_one_open")));
        REQUIRE(raw.exec(QStringLiteral("UPDATE day_log SET archived = 0")));
    }
    REQUIRE(store.open().has_value());
    CHECK(store.repo().open_log().error().m_Kind == StorageErrorKind::CORRUPT);
    CHECK(store.repo().create_log(log_id(3), sample_header()).error().m_Kind == StorageErrorKind::CORRUPT);
}

TEST_CASE("A log id that is not an id is reported when the open log is looked for", "[sqlite][damage]")
{
    Store store;
    store.start();
    store.m_Repository.reset();
    {
        RawDb raw(store.database());
        REQUIRE(raw.exec(QStringLiteral("UPDATE day_log SET id = 'nonsense'")));
    }
    REQUIRE(store.open().has_value());
    CHECK(store.repo().open_log().error().m_Kind == StorageErrorKind::CORRUPT);
    CHECK(store.repo().list_logs().error().m_Kind == StorageErrorKind::CORRUPT);
}

TEST_CASE("With a table missing, every call reports an error and no journal record is left behind", "[sqlite][damage]")
{
    for (const char* table :
         {"entry", "error_entry", "note_entry", "header_change_entry", "draft", "store_state", "day_log"})
    {
        INFO(table);
        Store store;
        store.start();
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 10)).has_value());
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_note(2, 11, "note")).has_value());
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(3, 12, HeaderField::UNIT_NUMBER, "28")).has_value());
        REQUIRE(store.repo().set_draft("draft", sample_time(1)).has_value());
        {
            RawDb raw(store.database());
            REQUIRE(raw.exec(QStringLiteral("DROP TABLE %1").arg(QLatin1String(table))));
        }

        // The records the journal holds that were not cancelled.
        const auto kept = [&store]
        {
            std::ptrdiff_t appended = 0;
            std::ptrdiff_t cancelled = 0;
            for (const auto& event : store.m_Journal.m_Events)
            {
                appended += event.m_Call == Call::APPEND ? 1 : 0;
                cancelled += event.m_Call == Call::CANCEL ? 1 : 0;
            }
            return appended - cancelled;
        };
        // A call either succeeds and leaves its records, or fails and leaves none.
        const auto settled = [&](bool ok, std::ptrdiff_t records_if_ok, std::ptrdiff_t& before)
        {
            CHECK(kept() - before == (ok ? records_if_ok : 0));
            before = kept();
        };
        std::ptrdiff_t before = kept();

        (void)store.repo().open_log();
        (void)store.repo().list_logs();
        (void)store.repo().load_log(log_id(LOG));
        (void)store.repo().get_draft();
        (void)store.repo().set_draft("again", sample_time(2));
        (void)store.repo().set_draft("", sample_time(3));
        (void)store.repo().store_applied_seq();
        (void)store.repo().journal_generation();
        (void)store.repo().applied_seq(log_id(LOG));
        settled(true, 0, before);

        settled(store.repo().add_entry(log_id(LOG), sample_error(10, 20)).has_value(), 1, before);
        settled(store.repo().add_entry(log_id(LOG), sample_note(11, 21, "n")).has_value(), 1, before);
        settled(store.repo().add_entry(log_id(LOG), sample_change(12, 22, HeaderField::UNIT_NUMBER, "29")).has_value(),
                1, before);
        settled(store.repo().edit_entry(log_id(LOG), sample_error(1, 23), sample_time(9)).has_value(), 1, before);
        settled(store.repo().edit_entry(log_id(LOG), sample_note(1, 23, "was an error"), sample_time(9)).has_value(), 1,
                before);
        settled(store.repo().set_entry_deleted(log_id(LOG), entry_id(2), true).has_value(), 1, before);
        settled(store.repo().correct_header(log_id(LOG), HeaderField::UNIT_NUMBER, "30").has_value(), 1, before);
        settled(store.repo().mark_shared(log_id(LOG), sample_time(5)).has_value(), 1, before);
        settled(store.repo().create_log(log_id(4), sample_header()).has_value(), 1, before);
        settled(store.repo().start_new_log(log_id(LOG), log_id(5), sample_header()).has_value(), 2, before);
        settled(store.repo().archive_log(log_id(LOG)).has_value(), 1, before);
        settled(
            store.repo().hard_delete_log(log_id(LOG), HistoryDeleteConfirmation::confirmed(log_id(LOG))).has_value(), 0,
            before);
    }
}
