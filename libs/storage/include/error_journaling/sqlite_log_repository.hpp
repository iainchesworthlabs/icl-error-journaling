// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/crash_point.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/log_repository.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>

namespace ej
{

/// The identity of our database file: `PRAGMA application_id` ("EJLG"). A SQLite file with another value is not ours
/// and is never opened for writing, let alone rebuilt (C14).
inline constexpr std::int32_t SQLITE_APPLICATION_ID = 0x454A4C47;

/// The schema this build writes: `PRAGMA user_version`. A database with a higher value was written by a newer build and
/// is refused untouched (C16, M5).
inline constexpr std::int32_t SQLITE_SCHEMA_VERSION = 1;

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

private:
    struct Impl;
    explicit SqliteLogRepository(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_Impl;
};

}  // namespace ej
