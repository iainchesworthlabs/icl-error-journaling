// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/log_repository.hpp"
#include "support/event_log.hpp"

#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace ej::test
{

enum class RepositoryOp
{
    CREATE_LOG,
    OPEN_LOG,
    ARCHIVE_LOG,
    START_NEW_LOG,
    LIST_LOGS,
    LOAD_LOG,
    ADD_ENTRY,
    EDIT_ENTRY,
    SET_ENTRY_DELETED,
    CORRECT_HEADER,
    MARK_SHARED,
    HARD_DELETE_LOG,
    SET_DRAFT,
    GET_DRAFT
};

[[nodiscard]] std::string_view repository_op_name(RepositoryOp op);

/// True for the calls that change the store. Those are the ones that commit.
[[nodiscard]] bool is_mutating(RepositoryOp op);

/// One call that reached the repository.
struct RepositoryCall
{
    RepositoryOp m_Op = RepositoryOp::OPEN_LOG;
    std::thread::id m_Thread;
    bool m_Failed = false; // an injected failure: the call did not reach the wrapped repository
};

/// A `LogRepository` that watches another one (usually `MemoryLogRepository`):
///
///  - it records every call, with the thread it was made on, and writes a `COMMIT` event into the `EventLog` after each
///    mutating call that succeeded, so a test can see where the commit falls among the haptics and the confirmations;
///  - a test can make the next calls of an operation fail without touching the wrapped repository (a failed commit);
///  - a test can hold or slow a mutating call (`set_before_commit`, `set_commit_delay`) to look at the state between
///    "submitted" and "committed" on a real thread;
///  - after each mutating call it counts the open logs, so a test can show that starting a new log never passes
///    through a moment with none or two.
///
/// It is as thread safe as the wrapped repository: one thread makes the calls. The records are readable from any
/// thread.
class RecordingRepository final : public LogRepository
{
public:
    /// `log` may be null. Neither argument is owned.
    explicit RecordingRepository(LogRepository& inner, EventLog* log = nullptr) : m_Inner(inner), m_Log(log) {}

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

    /// The next `times` calls of `op` fail with `error` and change nothing. `times` of `ALWAYS` keeps failing until
    /// `stop_failing`.
    static constexpr std::size_t ALWAYS = static_cast<std::size_t>(-1);
    void fail_next(RepositoryOp op, std::size_t times = 1,
                   StorageError error = make_error(StorageErrorKind::IO, "injected failure"));
    void stop_failing(RepositoryOp op);

    /// Runs on the calling thread at the start of every mutating call, before anything else: a test blocks here on a
    /// latch to hold a commit back. It is called without the recorder's lock, so it may read the records.
    void set_before_commit(std::function<void(RepositoryOp)> hook);

    /// Every mutating call sleeps this long before it reaches the wrapped repository.
    void set_commit_delay(std::chrono::milliseconds delay);

    /// Every call that reached the repository, in order (an injected failure included).
    [[nodiscard]] std::vector<RepositoryCall> calls() const;
    [[nodiscard]] std::size_t call_count(RepositoryOp op) const;
    [[nodiscard]] std::size_t mutating_call_count() const;

    /// The distinct threads the calls were made on.
    [[nodiscard]] std::set<std::thread::id> threads() const;

    /// The number of open (not archived) logs after each mutating call, in order.
    [[nodiscard]] std::vector<std::size_t> open_log_counts() const;

    void clear_records();

private:
    /// Everything common to a call: the record, the hook, the delay, an injected failure. Returns the error to answer
    /// with, or nothing when the call goes through.
    std::optional<StorageError> enter(RepositoryOp op);
    void leave(RepositoryOp op, bool succeeded, const std::string& detail);

    template <typename Return, typename Call>
    Return through(RepositoryOp op, const std::string& detail, Call&& call);

    LogRepository& m_Inner;
    EventLog* m_Log;

    mutable std::mutex m_Mutex;
    std::vector<RepositoryCall> m_Calls;
    std::vector<std::size_t> m_OpenLogCounts;
    struct Failure
    {
        std::size_t m_Remaining = 0;
        StorageError m_Error;
    };
    std::map<RepositoryOp, Failure> m_Failures;
    std::function<void(RepositoryOp)> m_BeforeCommit;
    std::chrono::milliseconds m_CommitDelay{0};
};

} // namespace ej::test
