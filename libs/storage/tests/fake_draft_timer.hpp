// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/draft_store.hpp"
#include "error_journaling/memory_log_repository.hpp"

#include <chrono>
#include <cstddef>

namespace ej::test
{

/// A `DraftTimer` the test advances by hand: no test sleeps, and the wall clock has no part in it.
class FakeDraftTimer final : public DraftTimer
{
public:
    void start(std::chrono::milliseconds delay, Callback on_expired) override
    {
        m_Deadline = m_Now + delay;
        m_Callback = std::move(on_expired);
        m_Armed = true;
        ++m_Starts;
    }

    void stop() override
    {
        m_Armed = false;
        m_Callback = nullptr;
    }

    /// Moves monotonic time on, running the callback at the moment it falls due (and again if it re-arms inside the
    /// span).
    void advance(std::chrono::milliseconds by)
    {
        const auto target = m_Now + by;
        while (m_Armed && m_Deadline <= target)
        {
            m_Now = m_Deadline;
            m_Armed = false;
            const Callback callback = std::move(m_Callback);
            m_Callback = nullptr;
            callback();
        }
        m_Now = target;
    }

    [[nodiscard]] bool armed() const
    {
        return m_Armed;
    }
    [[nodiscard]] std::chrono::milliseconds now() const
    {
        return m_Now;
    }
    [[nodiscard]] std::size_t starts() const
    {
        return m_Starts;
    }

private:
    std::chrono::milliseconds m_Now{0};
    std::chrono::milliseconds m_Deadline{0};
    Callback m_Callback;
    bool m_Armed = false;
    std::size_t m_Starts = 0;
};

/// A wall clock that counts how often it is read. The draft store reads it once for each write and at no other time, so
/// the count is the number of writes.
class CountingClock final : public Clock
{
public:
    explicit CountingClock(UtcMillis start) : m_Time(start) {}

    [[nodiscard]] UtcMillis now() const override
    {
        ++m_Reads;
        return m_Time;
    }

    void set(UtcMillis time)
    {
        m_Time = time;
    }
    [[nodiscard]] std::size_t reads() const
    {
        return m_Reads;
    }

private:
    UtcMillis m_Time;
    mutable std::size_t m_Reads = 0;
};

/// The in-memory repository with a `set_draft` that can be made to fail, for the cases of a write that does not work.
class DraftFailingRepository final : public LogRepository
{
public:
    explicit DraftFailingRepository(const Clock& clock) : m_Inner(clock) {}

    bool m_FailDraftWrites = false;
    std::size_t m_DraftWrites = 0; // calls to set_draft, failed or not

    Result<void, StorageError> create_log(LogId id, const HeaderValues& header) override
    {
        return m_Inner.create_log(id, header);
    }
    Result<std::optional<StoredLog>, StorageError> open_log() override
    {
        return m_Inner.open_log();
    }
    Result<void, StorageError> archive_log(LogId id) override
    {
        return m_Inner.archive_log(id);
    }
    Result<void, StorageError> start_new_log(LogId old_log, LogId new_log, const HeaderValues& header) override
    {
        return m_Inner.start_new_log(old_log, new_log, header);
    }
    Result<std::vector<LogSummary>, StorageError> list_logs() override
    {
        return m_Inner.list_logs();
    }
    Result<StoredLog, StorageError> load_log(LogId id) override
    {
        return m_Inner.load_log(id);
    }
    Result<void, StorageError> add_entry(LogId log, const Entry& entry) override
    {
        return m_Inner.add_entry(log, entry);
    }
    Result<void, StorageError> edit_entry(LogId log, const Entry& entry, UtcMillis edited_at) override
    {
        return m_Inner.edit_entry(log, entry, edited_at);
    }
    Result<void, StorageError> set_entry_deleted(LogId log, EntryId entry, bool deleted) override
    {
        return m_Inner.set_entry_deleted(log, entry, deleted);
    }
    Result<void, StorageError> correct_header(LogId log, HeaderField field, std::string value) override
    {
        return m_Inner.correct_header(log, field, std::move(value));
    }
    Result<void, StorageError> mark_shared(LogId log, UtcMillis when) override
    {
        return m_Inner.mark_shared(log, when);
    }
    Result<void, StorageError> hard_delete_log(LogId id, const DeleteConfirmation& confirmation) override
    {
        return m_Inner.hard_delete_log(id, confirmation);
    }
    Result<void, StorageError> set_draft(std::string_view text, UtcMillis when) override
    {
        ++m_DraftWrites;
        if (m_FailDraftWrites)
        {
            return fail(make_error(StorageErrorKind::DISK_FULL, "the disk is full"));
        }
        return m_Inner.set_draft(text, when);
    }
    Result<std::optional<Draft>, StorageError> get_draft() override
    {
        return m_Inner.get_draft();
    }

private:
    MemoryLogRepository m_Inner;
};

} // namespace ej::test
