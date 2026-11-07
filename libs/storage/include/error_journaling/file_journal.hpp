// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/crash_point.hpp"
#include "error_journaling/journal.hpp"
#include "error_journaling/journal_file.hpp"
#include "error_journaling/journal_writer.hpp"
#include "error_journaling/storage_location.hpp"

#include <functional>
#include <map>
#include <memory>

namespace ej
{

/// Opens a journal file at a path (and locks it). The default is `open_journal_file`; tests wrap or replace it to watch
/// the calls or to make them fail.
using JournalFileOpener =
    std::function<Result<std::unique_ptr<JournalFile>, StorageError>(const std::filesystem::path&)>;

struct FileJournalOptions
{
    CrashHook m_Hook;
    JournalFileOpener m_Opener;  // empty: open_journal_file
};

/// The journal of record on disk (DS-02): one file per log, `journal/<log id>.jsonl` while the log is open and under
/// `journal/archive/` once it is archived (section 5.4). A log's file is opened and locked the first time it is needed
/// and stays open, with the lock, until the log is archived or removed or the journal is destroyed. A second
/// `FileJournal` on the same folders is refused (`LOCKED`) the first time it needs a file the first one holds.
///
/// Use from one thread.
class FileJournal final : public Journal
{
public:
    using Options = FileJournalOptions;

    explicit FileJournal(StorageLocation location, Options options = {});
    ~FileJournal() override;

    [[nodiscard]] Result<std::uint64_t, StorageError> next_seq(LogId log, std::uint64_t applied_seq) override;
    [[nodiscard]] Result<JournalMark, StorageError> append(const JournalRecord& record) override;
    [[nodiscard]] Result<void, StorageError> cancel(const JournalMark& mark) override;
    [[nodiscard]] Result<void, StorageError> archive(LogId log) override;
    [[nodiscard]] Result<void, StorageError> remove(LogId log) override;

    /// Where a log's journal is while the log is open, and once it is archived.
    [[nodiscard]] std::filesystem::path open_path(LogId log) const;
    [[nodiscard]] std::filesystem::path archived_path(LogId log) const;

private:
    /// The writer for a log, opening its file (in either folder) if it is not open yet. Null when there is no file and
    /// `create` is false.
    [[nodiscard]] Result<JournalWriter*, StorageError> writer_for(LogId log, bool create);

    StorageLocation m_Location;
    Options m_Options;
    std::map<LogId, std::unique_ptr<JournalWriter>> m_Writers;
};

}  // namespace ej
