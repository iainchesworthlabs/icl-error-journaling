// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/crash_point.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/journal_file.hpp"

#include <cstdint>
#include <memory>

namespace ej
{

/// The writer of one journal file (section 2.5): one line per record, a newline at the end of each, appended and synced
/// before the call returns. It refuses to append anything but the next sequence number of its own log.
///
/// A failure to write or sync cuts the file back to its length before the record, so that a half-written line is never
/// left in front of the next record. If even that fails the error is returned and the file is as the operating system
/// left it; recovery reads it as a torn tail.
class JournalWriter
{
public:
    /// Takes over `file` (which the caller has already locked) and reads what is in it. The file must be clean: a torn
    /// tail, a bad line or a gap is left for recovery (`JOURNAL` error), because appending behind it would hide it. A
    /// file that is empty or holds only complete, consecutive records of `log` is fine.
    [[nodiscard]] static Result<std::unique_ptr<JournalWriter>, StorageError> open(std::unique_ptr<JournalFile> file,
                                                                                   LogId log, CrashHook hook = {});

    /// The sequence number of the last record in the file, 0 for an empty file.
    [[nodiscard]] std::uint64_t last_seq() const { return m_LastSeq; }
    [[nodiscard]] std::uint64_t size() const { return m_File->size(); }

    /// Appends the record and syncs it. Crash points: after the append, and after the sync.
    [[nodiscard]] Result<JournalMark, StorageError> append(const JournalRecord& record);

    /// Takes back the record `mark` describes: appends a `void` record that names it and syncs, or, if that fails, cuts
    /// the file back to `mark.m_Offset` and syncs (section 2.6). The record must be the last one.
    [[nodiscard]] Result<void, StorageError> cancel(const JournalMark& mark);

private:
    JournalWriter(std::unique_ptr<JournalFile> file, LogId log, CrashHook hook, std::uint64_t last_seq)
        : m_File(std::move(file)), m_Log(log), m_Hook(std::move(hook)), m_LastSeq(last_seq)
    {
    }

    [[nodiscard]] Result<JournalMark, StorageError> append_line(const JournalRecord& record, bool with_hooks);
    [[nodiscard]] Result<void, StorageError> cut_back(std::uint64_t length);

    std::unique_ptr<JournalFile> m_File;
    LogId m_Log;
    CrashHook m_Hook;
    std::uint64_t m_LastSeq;
};

}  // namespace ej
