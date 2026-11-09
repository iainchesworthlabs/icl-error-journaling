// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/header_field.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/journal_record.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/stored_log.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ej
{

/// Proof that the person confirmed deleting a log in the History screen (DS-05). `hard_delete_log` takes one, and the
/// only way to make one is `HistoryDeleteConfirmation::confirmed`, which the History screen's confirm handler calls and
/// nothing else should. Nothing in the app deletes an open log: there is no API for it.
class DeleteConfirmation
{
public:
    [[nodiscard]] LogId log() const { return m_Log; }

private:
    friend class HistoryDeleteConfirmation;
    explicit DeleteConfirmation(LogId log) : m_Log(log) {}

    LogId m_Log;
};

/// The History screen's confirmation path (Stage 4): call `confirmed` from the handler of the delete confirmation
/// button, and from nowhere else.
class HistoryDeleteConfirmation
{
public:
    [[nodiscard]] static DeleteConfirmation confirmed(LogId log) { return DeleteConfirmation(log); }
};

/// One line of the list of logs (DAY-04).
struct LogSummary
{
    LogId m_Id;
    HeaderValues m_Header;  // the values the log started with: date, facilitator, unit number, location
    UtcMillis m_CreatedUtc;
    std::optional<UtcMillis> m_LastSharedUtc;
    bool m_ChangedSinceShare = false; // `LogMeta::m_ChangedSinceShare`
    bool m_Archived = false;
    std::size_t m_LiveEntryCount = 0;   // not soft-deleted
    std::size_t m_TotalEntryCount = 0;  // live and deleted
};

/// `is_unshared` for a line of the list.
[[nodiscard]] constexpr bool is_unshared(const LogSummary& summary) noexcept
{
    return is_unshared(summary.m_LastSharedUtc, summary.m_ChangedSinceShare, summary.m_LiveEntryCount);
}

/// The note field's uncommitted text (NOTE-03).
struct Draft
{
    std::string m_Text;
    UtcMillis m_UpdatedUtc;
};

/// The storage of logs. Every mutating call is one atomic unit: one journal record and one database transaction, and
/// it returns success only after both are durable (DS-01, DS-02). Entry and log ids come from the caller (UUID v4),
/// which makes replay idempotent. Nothing throws across this boundary.
///
/// Not thread safe: use one repository from one thread.
class LogRepository
{
public:
    LogRepository() = default;
    LogRepository(const LogRepository&) = delete;
    LogRepository& operator=(const LogRepository&) = delete;
    virtual ~LogRepository() = default;

    /// Starts the first log, or one after the previous was archived. Refused with OPEN_LOG_EXISTS while a log is open.
    virtual Result<void, StorageError> create_log(LogId id, const HeaderValues& header) = 0;

    /// The open log, or nothing before the first log is started.
    [[nodiscard]] virtual Result<std::optional<StoredLog>, StorageError> open_log() = 0;

    /// Marks the log archived. The log stays readable and editable (DAY-04).
    virtual Result<void, StorageError> archive_log(LogId id) = 0;

    /// Starting a new day or log: archives the open log `old_log` and creates `new_log`. One unit: a failure or a kill
    /// leaves either the old log open or the new one, never none and never two.
    virtual Result<void, StorageError> start_new_log(LogId old_log, LogId new_log, const HeaderValues& header) = 0;

    /// Newest first (local date, then creation time).
    [[nodiscard]] virtual Result<std::vector<LogSummary>, StorageError> list_logs() = 0;
    [[nodiscard]] virtual Result<StoredLog, StorageError> load_log(LogId id) = 0;

    /// Adds an entry. Adding a note entry (NOTE-01) also clears the note draft, in the same transaction, so a kill
    /// cannot leave both the entry and the draft. An error entry carries the label of its type as a snapshot (decision
    /// 5).
    virtual Result<void, StorageError> add_entry(LogId log, const Entry& entry) = 0;
    virtual Result<void, StorageError> edit_entry(LogId log, const Entry& entry, UtcMillis edited_at) = 0;
    virtual Result<void, StorageError> set_entry_deleted(LogId log, EntryId entry, bool deleted) = 0;

    /// Correct (writes no entry). Change is `add_entry` of a header change entry.
    virtual Result<void, StorageError> correct_header(LogId log, HeaderField field, std::string value) = 0;

    virtual Result<void, StorageError> mark_shared(LogId log, UtcMillis when) = 0;

    /// Removes an archived log and its journal. The confirmation must be for this log (DS-05).
    virtual Result<void, StorageError> hard_delete_log(LogId id, const DeleteConfirmation& confirmation) = 0;

    /// An empty string clears the draft. The draft is not journalled (section 2.8).
    virtual Result<void, StorageError> set_draft(std::string_view text, UtcMillis when) = 0;
    [[nodiscard]] virtual Result<std::optional<Draft>, StorageError> get_draft() = 0;
};

}  // namespace ej
