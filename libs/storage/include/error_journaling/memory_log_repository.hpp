// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/log_repository.hpp"

#include <map>
#include <optional>
#include <vector>

namespace ej
{

/// `LogRepository` over a map, for tests of the core and the services above it that need no database. The typed
/// contract suite (`libs/storage/tests/test_repository_contract.cpp`) runs every case against this and against the
/// SQLite repository, so this one cannot drift from the real one. It applies the same `apply_record` rules as the real
/// one and keeps the records it would have journalled, which tests read through `records`.
///
/// Nothing is persistent; "durable" here means "visible to the next call".
class MemoryLogRepository final : public LogRepository
{
public:
    explicit MemoryLogRepository(const Clock& clock) : m_Clock(clock) {}

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

    /// The records this repository accepted for a log, in sequence order. Empty for an unknown log.
    [[nodiscard]] const std::vector<JournalRecord>& records(LogId id) const;

private:
    struct Slot
    {
        StoredLog m_Log;
        std::vector<JournalRecord> m_Records;
    };

    [[nodiscard]] const Slot* open_slot() const;
    Result<Slot*, StorageError> find_slot(LogId id);
    /// Applies one non-create record to a copy of the log and keeps the copy only when it succeeds.
    Result<void, StorageError> commit(LogId id, JournalBody body, bool clear_draft = false);
    Result<void, StorageError> insert_new(LogId id, const HeaderValues& header, std::optional<LogId> previous);

    const Clock& m_Clock;
    std::map<LogId, Slot> m_Logs;
    std::optional<Draft> m_Draft;
};

}  // namespace ej
