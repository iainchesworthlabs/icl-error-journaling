// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/journal_record.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/stored_log.hpp"

#include <filesystem>
#include <vector>

namespace ej
{

/// The records that rebuild `log` exactly: its `create_log`, an `add_entry` for each entry in order carrying its final
/// state (deleted, edited), the time it was shared, and `archive` if it is archived. Used where a journal has to be
/// written from a log that is known to be right: a journal that is missing, behind the database, or damaged in the
/// middle (J4, J5, J3). Replaying them (`ApplyMode::REPLAY`) gives the same log.
[[nodiscard]] std::vector<JournalRecord> seed_records(const StoredLog& log);

/// Writes `records` as a new journal at `path`: to a temporary file beside it, synced, then renamed over `path`, and
/// the folder synced. A kill leaves the old file or the new one, never half of either.
[[nodiscard]] Result<void, StorageError> write_journal_file(const std::filesystem::path& path,
                                                            const std::vector<JournalRecord>& records);

/// Cuts a journal back to `length` bytes (a torn tail removed) and syncs it.
[[nodiscard]] Result<void, StorageError> truncate_journal_file(const std::filesystem::path& path, std::uint64_t length);

}  // namespace ej
