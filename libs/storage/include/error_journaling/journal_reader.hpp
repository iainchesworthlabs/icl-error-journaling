// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"
#include "error_journaling/journal_record.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ej
{

/// Where a journal is read from, a piece at a time, so that a damaged file of any size is never held in memory whole.
class ByteSource
{
public:
    ByteSource() = default;
    ByteSource(const ByteSource&) = delete;
    ByteSource& operator=(const ByteSource&) = delete;
    virtual ~ByteSource() = default;

    /// Reads up to `max` bytes into `out`; 0 at the end.
    [[nodiscard]] virtual Result<std::size_t, StorageError> read(char* out, std::size_t max) = 0;
};

/// Reads a file without locking it (a writer in the same or another process may hold it open).
[[nodiscard]] Result<std::unique_ptr<ByteSource>, StorageError> open_file_source(const std::filesystem::path& path);

/// Reads bytes already in memory.
[[nodiscard]] std::unique_ptr<ByteSource> make_string_source(std::string_view bytes);

/// What a journal holds, read by the rules of section 5.2 of the Stage 2 plan.
///
/// A record is complete only when it ends with a newline and its checksum matches. The accepted records are the longest
/// run that starts at sequence number 1 and has no gap; a copy of a record already accepted is dropped. A line that is
/// not a record is counted and skipped: if it was a record, the next one shows a gap, and at the first gap the accepted
/// run ends, because a log of record must not skip a hole and carry on as if it were whole. The valid records after
/// that point are kept in `m_Aside`, for the caller to decide about.
struct JournalScan
{
    std::optional<LogId> m_Log;            // the log the records belong to (that of the first record)
    std::vector<JournalRecord> m_Records;  // accepted, in sequence order, voids and the records they cancel included
    std::vector<JournalRecord> m_Aside;    // valid records after the first problem, in file order
    std::uint64_t m_FileBytes = 0;         // everything read
    std::uint64_t m_CompleteBytes = 0;     // up to and including the last newline
    std::uint64_t m_TornTailBytes = 0;     // after the last newline: a record cut short, zero fill, or nothing useful
    std::size_t m_BadRecords = 0;          // complete lines that are not records, or are too long, or conflict
    std::size_t m_DuplicateRecords = 0;    // copies of accepted records, dropped
    std::size_t m_ForeignRecords = 0;      // records of another log
    std::size_t m_UnsupportedRecords = 0;  // valid records of a version this build does not know
    bool m_HasProblem = false;             // the accepted run ended before the end of the file: a gap, or a conflict

    [[nodiscard]] std::uint64_t last_seq() const { return m_Records.empty() ? 0 : m_Records.back().m_Seq; }

    /// True when nothing in the file needs recovery: no torn tail, no bad line, no gap, nothing foreign. Copies of
    /// records already accepted are harmless.
    [[nodiscard]] bool is_clean() const { return m_TornTailBytes == 0 && !m_HasProblem && m_BadRecords == 0; }
};

/// Reads a journal. With `expected_log`, a record of another log is refused as an error (J9) and nothing is read after
/// it; without, it is counted and ends the accepted run.
[[nodiscard]] Result<JournalScan, StorageError> scan_journal(ByteSource& source,
                                                             std::optional<LogId> expected_log = std::nullopt);
[[nodiscard]] Result<JournalScan, StorageError> scan_journal_bytes(std::string_view bytes,
                                                                   std::optional<LogId> expected_log = std::nullopt);

/// The accepted records with the voided ones taken out (section 3.3): a `void` record cancels the earlier record it
/// names, and neither the void nor the record it cancels is applied.
struct VoidResolution
{
    std::vector<JournalRecord> m_Effective;
    std::size_t m_Voided = 0;       // records cancelled
    std::size_t m_OrphanVoids = 0;  // voids that name nothing earlier
};

[[nodiscard]] VoidResolution resolve_voids(std::span<const JournalRecord> records);

}  // namespace ej
