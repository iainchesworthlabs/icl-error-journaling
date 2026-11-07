// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/journal_record.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstddef>
#include <optional>
#include <string_view>

namespace ej
{

/// Limits that keep every journal record bounded (decision 7).
inline constexpr std::size_t MAX_NOTE_CODE_POINTS = 10'000;
inline constexpr std::size_t MAX_ENTRIES_PER_LOG = 5'000;
inline constexpr std::size_t MAX_LABEL_CODE_POINTS = 200;
inline constexpr std::size_t MAX_HEADER_TEXT_CODE_POINTS = 200;
inline constexpr std::size_t MAX_ZONE_BYTES = 64;

/// What a log is besides its header and entries.
struct LogMeta
{
    LogId m_Id;
    UtcMillis m_CreatedUtc;
    std::optional<UtcMillis> m_LastSharedUtc;
    bool m_Archived = false;
    std::optional<LogId> m_Previous;
};

/// A log as storage holds it: the domain `Log` and its metadata. Iterating `m_Log.entries()` goes in journal sequence
/// order, so `sorted_entries()` breaks equal times by `seq`. An error entry carries the snapshot label of its type.
struct StoredLog
{
    LogMeta m_Meta;
    Log m_Log;
};

/// The one definition of what every operation does to a log. The in-memory repository applies it to its model, the
/// SQLite repository applies it to the log it loaded before writing the change, and recovery replays journals through
/// it, so the three cannot drift apart.
///
/// The rules: only the open log accepts a new entry (an archived log can still be edited, have entries deleted and
/// restored, be corrected and be shared); ids are unique; text is well-formed UTF-8 and within its limit; an error
/// entry has a non-empty snapshot label for its type; a header change goes through `change_header`, so its value is
/// validated and must differ from the value in force.

/// A new log from the first record of a journal.
[[nodiscard]] Result<StoredLog, StorageError> make_stored_log(LogId id, const CreateLogBody& body);

/// How strictly a record is checked as it is applied.
enum class ApplyMode
{
    /// A write the person just made: a new header change must differ from the value in force, and cannot arrive deleted
    /// or edited.
    STRICT,
    /// A record that was valid when it was written, applied again by recovery or built to describe a log as it now is:
    /// a header change is checked for its field and value only. (The log may legitimately hold a change equal to the
    /// value before it, after a restore or a correction.)
    REPLAY
};

/// Applies any record except `CREATE_LOG`. `VOID_RECORD` does nothing here: the code that reads a journal removes the
/// voided record before applying (it knows both seq numbers).
[[nodiscard]] Result<void, StorageError> apply_record(StoredLog& log, const JournalRecord& record,
                                                      ApplyMode mode = ApplyMode::STRICT);

/// A summary of a log that two copies of it can be compared by (section 2.7 step 5): how many entries it holds and a
/// hash of everything about it that a journal records.
struct StateDigest
{
    std::size_t m_Entries = 0;
    std::uint64_t m_Hash = 0;

    friend bool operator==(const StateDigest&, const StateDigest&) = default;
};

[[nodiscard]] StateDigest digest_of(const StoredLog& log);

/// Checks an entry against the rules above without applying it.
[[nodiscard]] Result<void, StorageError> validate_entry(const Entry& entry);

/// Checks a header against the length rules.
[[nodiscard]] Result<void, StorageError> validate_header(const HeaderValues& header);

/// Checks the note draft: well-formed UTF-8 and no longer than a note may be.
[[nodiscard]] Result<void, StorageError> validate_draft(std::string_view text);

}  // namespace ej
