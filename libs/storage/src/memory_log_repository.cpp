// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/memory_log_repository.hpp"

#include "error_journaling/utf8.hpp"

#include <algorithm>

namespace ej
{

namespace
{

using Failure = std::unexpected<StorageError>;

Failure not_found(std::string message)
{
    return Failure(make_error(StorageErrorKind::NOT_FOUND, std::move(message)));
}

}  // namespace

const MemoryLogRepository::Slot* MemoryLogRepository::open_slot() const
{
    for (const auto& [id, slot] : m_Logs)
    {
        if (!slot.m_Log.m_Meta.m_Archived)
        {
            return &slot;
        }
    }
    return nullptr;
}

Result<MemoryLogRepository::Slot*, StorageError> MemoryLogRepository::find_slot(LogId id)
{
    const auto it = m_Logs.find(id);
    if (it == m_Logs.end())
    {
        return not_found("no such log");
    }
    return &it->second;
}

const std::vector<JournalRecord>& MemoryLogRepository::records(LogId id) const
{
    static const std::vector<JournalRecord> NONE;
    const auto it = m_Logs.find(id);
    return it == m_Logs.end() ? NONE : it->second.m_Records;
}

Result<void, StorageError> MemoryLogRepository::insert_new(LogId id, const HeaderValues& header,
                                                           std::optional<LogId> previous)
{
    CreateLogBody body{header, m_Clock.now(), previous};
    auto stored = make_stored_log(id, body);
    if (!stored)
    {
        return Failure(stored.error());
    }
    JournalRecord record;
    record.m_Seq = 1;
    record.m_Log = id;
    record.m_Body = std::move(body);
    Slot slot{std::move(*stored), {}};
    slot.m_Records.push_back(std::move(record));
    m_Logs.emplace(id, std::move(slot));
    return {};
}

Result<void, StorageError> MemoryLogRepository::create_log(LogId id, const HeaderValues& header)
{
    if (m_Logs.contains(id))
    {
        return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a log with this id exists"));
    }
    if (open_slot() != nullptr)
    {
        return Failure(make_error(StorageErrorKind::OPEN_LOG_EXISTS, "a log is open"));
    }
    return insert_new(id, header, std::nullopt);
}

Result<std::optional<StoredLog>, StorageError> MemoryLogRepository::open_log()
{
    const Slot* slot = open_slot();
    if (slot == nullptr)
    {
        return std::optional<StoredLog>{};
    }
    return std::optional<StoredLog>{slot->m_Log};
}

Result<void, StorageError> MemoryLogRepository::commit(LogId id, JournalBody body, bool clear_draft)
{
    auto slot = find_slot(id);
    if (!slot)
    {
        return Failure(slot.error());
    }
    JournalRecord record;
    record.m_Seq = (*slot)->m_Records.size() + 1;
    record.m_Log = id;
    record.m_Body = std::move(body);

    StoredLog copy = (*slot)->m_Log;
    if (auto applied = apply_record(copy, record); !applied)
    {
        return applied;
    }
    (*slot)->m_Log = std::move(copy);
    (*slot)->m_Records.push_back(std::move(record));
    if (clear_draft)
    {
        m_Draft.reset();
    }
    return {};
}

Result<void, StorageError> MemoryLogRepository::archive_log(LogId id)
{
    return commit(id, ArchiveBody{});
}

Result<void, StorageError> MemoryLogRepository::start_new_log(LogId old_log, LogId new_log, const HeaderValues& header)
{
    const Slot* open = open_slot();
    if (open == nullptr || open->m_Log.m_Meta.m_Id != old_log)
    {
        return Failure(make_error(StorageErrorKind::NOT_OPEN_LOG, "the log to archive is not the open log"));
    }
    if (m_Logs.contains(new_log))
    {
        return Failure(make_error(StorageErrorKind::ALREADY_EXISTS, "a log with this id exists"));
    }
    if (auto ok = validate_header(header); !ok)
    {
        return ok;
    }
    // Both or neither.
    const auto saved = m_Logs;
    if (auto archived = commit(old_log, ArchiveBody{}); !archived)
    {
        return archived;
    }
    if (auto created = insert_new(new_log, header, old_log); !created)
    {
        m_Logs = saved;
        return created;
    }
    return {};
}

Result<std::vector<LogSummary>, StorageError> MemoryLogRepository::list_logs()
{
    std::vector<LogSummary> result;
    result.reserve(m_Logs.size());
    for (const auto& [id, slot] : m_Logs)
    {
        const StoredLog& log = slot.m_Log;
        LogSummary summary;
        summary.m_Id = id;
        summary.m_Header = log.m_Log.initial_header();
        summary.m_CreatedUtc = log.m_Meta.m_CreatedUtc;
        summary.m_LastSharedUtc = log.m_Meta.m_LastSharedUtc;
        summary.m_Archived = log.m_Meta.m_Archived;
        summary.m_TotalEntryCount = log.m_Log.entries().size();
        summary.m_LiveEntryCount = log.m_Log.sorted_entries(false).size();
        result.push_back(std::move(summary));
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const LogSummary& a, const LogSummary& b)
                     {
                         if (a.m_Header.m_LocalDate != b.m_Header.m_LocalDate)
                         {
                             return a.m_Header.m_LocalDate > b.m_Header.m_LocalDate;
                         }
                         return a.m_CreatedUtc > b.m_CreatedUtc;
                     });
    return result;
}

Result<StoredLog, StorageError> MemoryLogRepository::load_log(LogId id)
{
    auto slot = find_slot(id);
    if (!slot)
    {
        return Failure(slot.error());
    }
    return (*slot)->m_Log;
}

Result<void, StorageError> MemoryLogRepository::add_entry(LogId log, const Entry& entry)
{
    return commit(log, AddEntryBody{entry}, std::holds_alternative<NoteEntry>(entry.m_Body));
}

Result<void, StorageError> MemoryLogRepository::edit_entry(LogId log, const Entry& entry, UtcMillis edited_at)
{
    return commit(log, EditEntryBody{entry, edited_at});
}

Result<void, StorageError> MemoryLogRepository::set_entry_deleted(LogId log, EntryId entry, bool deleted)
{
    return commit(log, SetDeletedBody{entry, deleted});
}

Result<void, StorageError> MemoryLogRepository::correct_header(LogId log, HeaderField field, std::string value)
{
    return commit(log, CorrectHeaderBody{field, std::move(value)});
}

Result<void, StorageError> MemoryLogRepository::mark_shared(LogId log, UtcMillis when)
{
    return commit(log, MarkSharedBody{when});
}

Result<void, StorageError> MemoryLogRepository::hard_delete_log(LogId id, const DeleteConfirmation& confirmation)
{
    if (confirmation.log() != id)
    {
        return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, "the confirmation is for another log"));
    }
    auto slot = find_slot(id);
    if (!slot)
    {
        return Failure(slot.error());
    }
    if (!(*slot)->m_Log.m_Meta.m_Archived)
    {
        return Failure(make_error(StorageErrorKind::LOG_NOT_ARCHIVED, "only an archived log can be deleted"));
    }
    m_Logs.erase(id);
    return {};
}

Result<void, StorageError> MemoryLogRepository::set_draft(std::string_view text, UtcMillis when)
{
    const auto count = utf8_code_points(text);
    if (!count)
    {
        return Failure(make_error(StorageErrorKind::INVALID_ARGUMENT, "the draft is not well-formed UTF-8"));
    }
    if (*count > MAX_NOTE_CODE_POINTS)
    {
        return Failure(make_error(StorageErrorKind::LIMIT_EXCEEDED, "the draft is longer than a note may be"));
    }
    if (text.empty())
    {
        m_Draft.reset();
    }
    else
    {
        m_Draft = Draft{std::string(text), when};
    }
    return {};
}

Result<std::optional<Draft>, StorageError> MemoryLogRepository::get_draft()
{
    return m_Draft;
}

}  // namespace ej
