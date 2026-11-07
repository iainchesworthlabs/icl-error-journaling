// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/recording_repository.hpp"

#include <algorithm>
#include <utility>

namespace ej::test
{

std::string_view repository_op_name(RepositoryOp op)
{
    switch (op)
    {
    case RepositoryOp::CREATE_LOG:
        return "create_log";
    case RepositoryOp::OPEN_LOG:
        return "open_log";
    case RepositoryOp::ARCHIVE_LOG:
        return "archive_log";
    case RepositoryOp::START_NEW_LOG:
        return "start_new_log";
    case RepositoryOp::LIST_LOGS:
        return "list_logs";
    case RepositoryOp::LOAD_LOG:
        return "load_log";
    case RepositoryOp::ADD_ENTRY:
        return "add_entry";
    case RepositoryOp::EDIT_ENTRY:
        return "edit_entry";
    case RepositoryOp::SET_ENTRY_DELETED:
        return "set_entry_deleted";
    case RepositoryOp::CORRECT_HEADER:
        return "correct_header";
    case RepositoryOp::MARK_SHARED:
        return "mark_shared";
    case RepositoryOp::HARD_DELETE_LOG:
        return "hard_delete_log";
    case RepositoryOp::SET_DRAFT:
        return "set_draft";
    case RepositoryOp::GET_DRAFT:
        return "get_draft";
    }
    return "unknown";
}

bool is_mutating(RepositoryOp op)
{
    switch (op)
    {
    case RepositoryOp::OPEN_LOG:
    case RepositoryOp::LIST_LOGS:
    case RepositoryOp::LOAD_LOG:
    case RepositoryOp::GET_DRAFT:
        return false;
    case RepositoryOp::CREATE_LOG:
    case RepositoryOp::ARCHIVE_LOG:
    case RepositoryOp::START_NEW_LOG:
    case RepositoryOp::ADD_ENTRY:
    case RepositoryOp::EDIT_ENTRY:
    case RepositoryOp::SET_ENTRY_DELETED:
    case RepositoryOp::CORRECT_HEADER:
    case RepositoryOp::MARK_SHARED:
    case RepositoryOp::HARD_DELETE_LOG:
    case RepositoryOp::SET_DRAFT:
        return true;
    }
    return false;
}

std::optional<StorageError> RecordingRepository::enter(RepositoryOp op)
{
    std::optional<StorageError> injected;
    std::function<void(RepositoryOp)> hook;
    std::chrono::milliseconds delay{0};
    {
        const std::lock_guard lock(m_Mutex);
        const auto failure = m_Failures.find(op);
        if (failure != m_Failures.end() && failure->second.m_Remaining > 0)
        {
            injected = failure->second.m_Error;
            if (failure->second.m_Remaining != ALWAYS)
            {
                --failure->second.m_Remaining;
            }
        }
        m_Calls.push_back(RepositoryCall{op, std::this_thread::get_id(), injected.has_value()});
        if (is_mutating(op))
        {
            hook = m_BeforeCommit;
            delay = m_CommitDelay;
        }
    }
    if (hook)
    {
        hook(op);
    }
    if (delay > std::chrono::milliseconds::zero())
    {
        std::this_thread::sleep_for(delay);
    }
    return injected;
}

void RecordingRepository::leave(RepositoryOp op, bool succeeded, const std::string& detail)
{
    if (!is_mutating(op))
    {
        return;
    }
    if (succeeded && m_Log != nullptr)
    {
        m_Log->record(EventKind::COMMIT, detail);
    }
    std::size_t open = 0;
    const auto summaries = m_Inner.list_logs();
    if (!summaries)
    {
        return;
    }
    for (const LogSummary& summary : *summaries)
    {
        if (!summary.m_Archived)
        {
            ++open;
        }
    }
    const std::lock_guard lock(m_Mutex);
    m_OpenLogCounts.push_back(open);
}

template <typename Return, typename Call>
Return RecordingRepository::through(RepositoryOp op, const std::string& detail, Call&& call)
{
    if (auto injected = enter(op))
    {
        leave(op, false, detail);
        return fail(std::move(*injected));
    }
    Return result = call();
    leave(op, result.has_value(), detail);
    return result;
}

namespace
{

std::string describe_call(RepositoryOp op, const std::string& argument)
{
    std::string text(repository_op_name(op));
    if (!argument.empty())
    {
        text += ' ';
        text += argument;
    }
    return text;
}

} // namespace

Result<void, StorageError> RecordingRepository::create_log(LogId id, const HeaderValues& header)
{
    return through<Result<void, StorageError>>(RepositoryOp::CREATE_LOG,
                                               describe_call(RepositoryOp::CREATE_LOG, id.format()),
                                               [&] { return m_Inner.create_log(id, header); });
}

Result<std::optional<StoredLog>, StorageError> RecordingRepository::open_log()
{
    return through<Result<std::optional<StoredLog>, StorageError>>(RepositoryOp::OPEN_LOG, {},
                                                                   [&] { return m_Inner.open_log(); });
}

Result<void, StorageError> RecordingRepository::archive_log(LogId id)
{
    return through<Result<void, StorageError>>(RepositoryOp::ARCHIVE_LOG,
                                               describe_call(RepositoryOp::ARCHIVE_LOG, id.format()),
                                               [&] { return m_Inner.archive_log(id); });
}

Result<void, StorageError> RecordingRepository::start_new_log(LogId old_log, LogId new_log, const HeaderValues& header)
{
    return through<Result<void, StorageError>>(RepositoryOp::START_NEW_LOG,
                                               describe_call(RepositoryOp::START_NEW_LOG, new_log.format()),
                                               [&] { return m_Inner.start_new_log(old_log, new_log, header); });
}

Result<std::vector<LogSummary>, StorageError> RecordingRepository::list_logs()
{
    return through<Result<std::vector<LogSummary>, StorageError>>(RepositoryOp::LIST_LOGS, {},
                                                                  [&] { return m_Inner.list_logs(); });
}

Result<StoredLog, StorageError> RecordingRepository::load_log(LogId id)
{
    return through<Result<StoredLog, StorageError>>(RepositoryOp::LOAD_LOG, {}, [&] { return m_Inner.load_log(id); });
}

Result<void, StorageError> RecordingRepository::add_entry(LogId log, const Entry& entry)
{
    return through<Result<void, StorageError>>(RepositoryOp::ADD_ENTRY,
                                               describe_call(RepositoryOp::ADD_ENTRY, entry.m_Id.format()),
                                               [&] { return m_Inner.add_entry(log, entry); });
}

Result<void, StorageError> RecordingRepository::edit_entry(LogId log, const Entry& entry, UtcMillis edited_at)
{
    return through<Result<void, StorageError>>(RepositoryOp::EDIT_ENTRY,
                                               describe_call(RepositoryOp::EDIT_ENTRY, entry.m_Id.format()),
                                               [&] { return m_Inner.edit_entry(log, entry, edited_at); });
}

Result<void, StorageError> RecordingRepository::set_entry_deleted(LogId log, EntryId entry, bool deleted)
{
    return through<Result<void, StorageError>>(
        RepositoryOp::SET_ENTRY_DELETED,
        describe_call(RepositoryOp::SET_ENTRY_DELETED, entry.format() + (deleted ? " deleted" : " restored")),
        [&] { return m_Inner.set_entry_deleted(log, entry, deleted); });
}

Result<void, StorageError> RecordingRepository::correct_header(LogId log, HeaderField field, std::string value)
{
    return through<Result<void, StorageError>>(
        RepositoryOp::CORRECT_HEADER, describe_call(RepositoryOp::CORRECT_HEADER, std::string(header_field_key(field))),
        [&] { return m_Inner.correct_header(log, field, std::move(value)); });
}

Result<void, StorageError> RecordingRepository::mark_shared(LogId log, UtcMillis when)
{
    return through<Result<void, StorageError>>(RepositoryOp::MARK_SHARED,
                                               describe_call(RepositoryOp::MARK_SHARED, log.format()),
                                               [&] { return m_Inner.mark_shared(log, when); });
}

Result<void, StorageError> RecordingRepository::hard_delete_log(LogId id, const DeleteConfirmation& confirmation)
{
    return through<Result<void, StorageError>>(RepositoryOp::HARD_DELETE_LOG,
                                               describe_call(RepositoryOp::HARD_DELETE_LOG, id.format()),
                                               [&] { return m_Inner.hard_delete_log(id, confirmation); });
}

Result<void, StorageError> RecordingRepository::set_draft(std::string_view text, UtcMillis when)
{
    return through<Result<void, StorageError>>(RepositoryOp::SET_DRAFT, describe_call(RepositoryOp::SET_DRAFT, {}),
                                               [&] { return m_Inner.set_draft(text, when); });
}

Result<std::optional<Draft>, StorageError> RecordingRepository::get_draft()
{
    return through<Result<std::optional<Draft>, StorageError>>(RepositoryOp::GET_DRAFT, {},
                                                               [&] { return m_Inner.get_draft(); });
}

void RecordingRepository::fail_next(RepositoryOp op, std::size_t times, StorageError error)
{
    const std::lock_guard lock(m_Mutex);
    m_Failures[op] = Failure{times, std::move(error)};
}

void RecordingRepository::stop_failing(RepositoryOp op)
{
    const std::lock_guard lock(m_Mutex);
    m_Failures.erase(op);
}

void RecordingRepository::set_before_commit(std::function<void(RepositoryOp)> hook)
{
    const std::lock_guard lock(m_Mutex);
    m_BeforeCommit = std::move(hook);
}

void RecordingRepository::set_commit_delay(std::chrono::milliseconds delay)
{
    const std::lock_guard lock(m_Mutex);
    m_CommitDelay = delay;
}

std::vector<RepositoryCall> RecordingRepository::calls() const
{
    const std::lock_guard lock(m_Mutex);
    return m_Calls;
}

std::size_t RecordingRepository::call_count(RepositoryOp op) const
{
    const std::lock_guard lock(m_Mutex);
    return static_cast<std::size_t>(
        std::ranges::count_if(m_Calls, [op](const RepositoryCall& call) { return call.m_Op == op; }));
}

std::size_t RecordingRepository::mutating_call_count() const
{
    const std::lock_guard lock(m_Mutex);
    return static_cast<std::size_t>(
        std::ranges::count_if(m_Calls, [](const RepositoryCall& call) { return is_mutating(call.m_Op); }));
}

std::set<std::thread::id> RecordingRepository::threads() const
{
    const std::lock_guard lock(m_Mutex);
    std::set<std::thread::id> result;
    for (const RepositoryCall& call : m_Calls)
    {
        result.insert(call.m_Thread);
    }
    return result;
}

std::vector<std::size_t> RecordingRepository::open_log_counts() const
{
    const std::lock_guard lock(m_Mutex);
    return m_OpenLogCounts;
}

void RecordingRepository::clear_records()
{
    const std::lock_guard lock(m_Mutex);
    m_Calls.clear();
    m_OpenLogCounts.clear();
}

} // namespace ej::test
