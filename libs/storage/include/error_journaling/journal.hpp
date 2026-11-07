// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"
#include "error_journaling/journal_record.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstdint>

namespace ej
{

/// Where a record went, so that the write can be taken back if the database refuses it. The repository keeps it and
/// gives it back; what the numbers mean is the journal's business.
struct JournalMark
{
    LogId m_Log;
    std::uint64_t m_Seq = 0;
    std::uint64_t m_Offset = 0;  // the length of the journal before the record
};

/// The append-only journal of record (DS-02), seen from the repository. The repository appends a record and has it made
/// durable BEFORE it commits the database transaction, and acknowledges its caller only after both.
///
/// `NullJournal` keeps nothing. The file journal (`JournalWriter`) arrives with the platform sync and lock code.
class Journal
{
public:
    Journal() = default;
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;
    virtual ~Journal() = default;

    /// The sequence number for the next record of `log`: one past the last record the journal holds, or `applied_seq +
    /// 1` when that is higher (the database may be ahead of a journal that was seeded from it). A new log starts at 1.
    [[nodiscard]] virtual Result<std::uint64_t, StorageError> next_seq(LogId log, std::uint64_t applied_seq) = 0;

    /// Appends the record to the journal of `record.m_Log` and makes it durable before returning.
    [[nodiscard]] virtual Result<JournalMark, StorageError> append(const JournalRecord& record) = 0;

    /// The write the record described failed after the append: the user was told so, so the record must not come back.
    /// Writes a `void` record for it or, if that fails, truncates the journal to its length before the record.
    [[nodiscard]] virtual Result<void, StorageError> cancel(const JournalMark& mark) = 0;

    /// The log was archived: closes its journal and keeps it under `journal/archive/`.
    [[nodiscard]] virtual Result<void, StorageError> archive(LogId log) = 0;

    /// A confirmed hard delete of an archived log: removes its journal.
    [[nodiscard]] virtual Result<void, StorageError> remove(LogId log) = 0;
};

/// A journal that records nothing. Sequence numbers still advance, so the database sees the numbers a real journal
/// would give.
class NullJournal final : public Journal
{
public:
    [[nodiscard]] Result<std::uint64_t, StorageError> next_seq(LogId, std::uint64_t applied_seq) override
    {
        return applied_seq + 1;
    }
    [[nodiscard]] Result<JournalMark, StorageError> append(const JournalRecord& record) override
    {
        return JournalMark{record.m_Log, record.m_Seq, 0};
    }
    [[nodiscard]] Result<void, StorageError> cancel(const JournalMark&) override { return {}; }
    [[nodiscard]] Result<void, StorageError> archive(LogId) override { return {}; }
    [[nodiscard]] Result<void, StorageError> remove(LogId) override { return {}; }
};

}  // namespace ej
