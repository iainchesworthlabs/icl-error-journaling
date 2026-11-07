// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/journal.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <optional>
#include <vector>

namespace ej::test
{

/// A journal that remembers every call, can run a function while a record is being appended, and can be told to fail.
/// A cancelled record leaves a gap, as the file journal's void record does.
class RecordingJournal final : public Journal
{
public:
    enum class Call
    {
        NEXT_SEQ,
        APPEND,
        CANCEL,
        ARCHIVE,
        REMOVE
    };

    struct Event
    {
        Call m_Call;
        LogId m_Log;
        std::uint64_t m_Seq = 0;
    };

    [[nodiscard]] Result<std::uint64_t, StorageError> next_seq(LogId log, std::uint64_t applied_seq) override
    {
        m_Events.push_back({Call::NEXT_SEQ, log, 0});
        return std::max(m_Last[log], applied_seq) + 1;
    }

    [[nodiscard]] Result<JournalMark, StorageError> append(const JournalRecord& record) override
    {
        m_Events.push_back({Call::APPEND, record.m_Log, record.m_Seq});
        if (m_OnAppend)
        {
            m_OnAppend(record);
        }
        if (m_FailAppend)
        {
            return std::unexpected(*m_FailAppend);
        }
        m_Records.push_back(record);
        m_Last[record.m_Log] = record.m_Seq;
        return JournalMark{record.m_Log, record.m_Seq, 0};
    }

    [[nodiscard]] Result<void, StorageError> cancel(const JournalMark& mark) override
    {
        m_Events.push_back({Call::CANCEL, mark.m_Log, mark.m_Seq});
        if (m_FailCancel)
        {
            return std::unexpected(*m_FailCancel);
        }
        m_Cancelled.push_back(mark.m_Seq);
        m_Last[mark.m_Log] = mark.m_Seq + 1;  // the void record took the next number
        return {};
    }

    [[nodiscard]] Result<void, StorageError> archive(LogId log) override
    {
        m_Events.push_back({Call::ARCHIVE, log, 0});
        return {};
    }

    [[nodiscard]] Result<void, StorageError> remove(LogId log) override
    {
        m_Events.push_back({Call::REMOVE, log, 0});
        if (m_FailRemove)
        {
            return std::unexpected(*m_FailRemove);
        }
        return {};
    }

    /// The calls other than `next_seq`, in order.
    [[nodiscard]] std::vector<Call> calls() const
    {
        std::vector<Call> result;
        for (const Event& event : m_Events)
        {
            if (event.m_Call != Call::NEXT_SEQ)
            {
                result.push_back(event.m_Call);
            }
        }
        return result;
    }

    std::vector<Event> m_Events;
    std::vector<JournalRecord> m_Records;
    std::vector<std::uint64_t> m_Cancelled;
    std::map<LogId, std::uint64_t> m_Last;
    std::function<void(const JournalRecord&)> m_OnAppend;
    std::optional<StorageError> m_FailAppend;
    std::optional<StorageError> m_FailCancel;
    std::optional<StorageError> m_FailRemove;
};

}  // namespace ej::test
