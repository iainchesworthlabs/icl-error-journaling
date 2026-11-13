// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/crash_point.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/log_repository.hpp"

#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ej
{

/// The identity of our database file: `PRAGMA application_id` ("EJLG"). A SQLite file with another value is not ours
/// and is never opened for writing, let alone rebuilt (C14).
inline constexpr std::int32_t SQLITE_APPLICATION_ID = 0x454A4C47;

/// The schema this build writes: `PRAGMA user_version`. A database with a higher value was written by a newer build and
/// is refused untouched (C16, M5). Version 2 adds `day_log.changed_since_share`, version 3 `day_log.extra_versions`; an
/// older database is migrated
/// (`product_migration_steps`) before it is opened.
inline constexpr std::int32_t SQLITE_SCHEMA_VERSION = 3;

/// The answer of a SQLite check: `ok`, or SQLite's own description of what is wrong (pages and tables, never row
/// content).
struct CheckResult
{
    bool m_Ok = false;
    std::string m_Message;
};

/// What a replay did: records applied, and records skipped because the database already had them.
struct ReplayCounts
{
    std::size_t m_Applied = 0;
    std::size_t m_Skipped = 0;
};

struct SqliteOptions
{
    /// Called at each crash point (null in production); an error it returns fails that step.
    CrashHook m_Hook;
    int m_BusyTimeoutMs = 5000;
};

/// `LogRepository` over SQLite (WAL, synchronous FULL). Every mutating call is one `BEGIN IMMEDIATE` transaction: it
/// checks and applies the change, appends the journal record and has it made durable, and only then commits. A failure
/// before the commit rolls back and leaves nothing; a failure of the commit itself cancels the journal record
/// (section 2.6).
///
/// Use one repository from one thread: the connection belongs to the thread that opened it.
class SqliteLogRepository final : public LogRepository
{
public:
    using Options = SqliteOptions;

    /// Opens the database at `database`, creating and initialising it when the file is new or empty. The settings (WAL,
    /// synchronous FULL, foreign keys, busy timeout, full fsync) are set and then read back; the open fails when the
    /// database does not report what was asked for (Qt SQL ignores a pragma the driver cannot honour). A file that is
    /// not ours (`NOT_OUR_DATABASE`), is newer (`NEWER_SCHEMA`), is not a database (`CORRUPT`), is locked (`LOCKED`) or
    /// cannot be read (`PERMISSION`) is reported as such and left untouched.
    ///
    /// The clock and the journal must outlive the repository.
    [[nodiscard]] static Result<std::unique_ptr<SqliteLogRepository>, StorageError>
    open(const std::filesystem::path& database, const Clock& clock, Journal& journal, Options options = {});

    ~SqliteLogRepository() override;

    Result<void, StorageError> create_log(LogId id, const HeaderValues& header) override;
    Result<std::optional<StoredLog>, StorageError> open_log() override;
    Result<void, StorageError> archive_log(LogId id) override;
    Result<void, StorageError> start_new_log(LogId old_log, LogId new_log, const HeaderValues& header) override;
    Result<std::vector<LogSummary>, StorageError> list_logs() override;
    Result<StoredLog, StorageError> load_log(LogId id) override;
    Result<void, StorageError> add_entry(LogId log, const Entry& entry) override;
    Result<void, StorageError> edit_entry(LogId log, const Entry& entry, UtcMillis edited_at) override;
    Result<void, StorageError> set_entry_deleted(LogId log, EntryId entry, bool deleted) override;
    Result<void, StorageError> correct_header(LogId log, HeaderField field, std::string value) override;
    Result<void, StorageError> mark_shared(LogId log, UtcMillis when) override;
    Result<void, StorageError> hard_delete_log(LogId id, const DeleteConfirmation& confirmation) override;
    Result<void, StorageError> set_draft(std::string_view text, UtcMillis when) override;
    Result<std::optional<Draft>, StorageError> get_draft() override;

    /// The highest journal sequence number of `log` that is committed to this database (0 for none).
    [[nodiscard]] Result<std::uint64_t, StorageError> applied_seq(LogId log);

    /// `store_state.applied_seq`: the applied sequence number of the open log, 0 when there is none.
    [[nodiscard]] Result<std::uint64_t, StorageError> store_applied_seq();

    /// `store_state.journal_generation`: how many logs this store has created.
    [[nodiscard]] Result<std::uint64_t, StorageError> journal_generation();

    // --- for recovery (section 2.7) ---

    /// `PRAGMA quick_check` and `PRAGMA integrity_check`. A database that is damaged enough to fail the statement
    /// itself answers with a `CORRUPT` error instead.
    [[nodiscard]] Result<CheckResult, StorageError> quick_check();
    [[nodiscard]] Result<CheckResult, StorageError> integrity_check();

    /// Every log in the database with the highest journal sequence number committed to it.
    [[nodiscard]] Result<std::map<LogId, std::uint64_t>, StorageError> applied_seqs();

    /// Applies journal records to the database in one transaction and writes nothing to the journal (they come from
    /// it). The records are in the order they must be applied, a log's `create_log` before anything else of it. A
    /// record at or below what the database holds for its log is skipped, and so is an `add_entry` of an entry that is
    /// there already and the `archive` of a log that is archived, so applying the same records twice leaves the same
    /// log (idempotence). Records are applied with `ApplyMode::REPLAY`. Replaying a note that the database did not have
    /// also removes a draft written no later than the note was captured: the commit that never reached the database would
    /// have cleared it in the same transaction, and the draft is not journalled.
    [[nodiscard]] Result<ReplayCounts, StorageError> replay(const std::vector<JournalRecord>& records);

    /// Replaces everything the database holds about `log` with `log`, and records `applied_seq` for it. Used where the
    /// database differs from the journal that is the log of record.
    [[nodiscard]] Result<void, StorageError> replace_log(const StoredLog& log, std::uint64_t applied_seq);

    /// Records that the database has applied the journal of `log` up to `seq`, after the journal was renumbered.
    [[nodiscard]] Result<void, StorageError> set_applied_seq(LogId log, std::uint64_t seq);

    /// Folds the write-ahead log into the database file and empties it, so that the file can be moved on its own.
    [[nodiscard]] Result<void, StorageError> checkpoint();

    /// The note draft of a database that failed its checks, read on a read-only connection of its own. Nothing when it
    /// cannot be read (the draft is not journalled, so a damaged database can lose it).
    [[nodiscard]] static std::optional<Draft> salvage_draft(const std::filesystem::path& database);

private:
    struct Impl;
    explicit SqliteLogRepository(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_Impl;
};

}  // namespace ej
