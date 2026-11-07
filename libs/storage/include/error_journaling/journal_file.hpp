// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace ej
{

/// One journal file as the writer sees it: bytes appended at the end, a sync that makes them durable, and a way to cut
/// the file back. The real file is `open_journal_file`; the power-loss tests use a double that keeps unsynced bytes
/// apart
/// (`LossyFile`), which is why this is an interface.
class JournalFile
{
public:
    JournalFile() = default;
    JournalFile(const JournalFile&) = delete;
    JournalFile& operator=(const JournalFile&) = delete;
    virtual ~JournalFile() = default;

    [[nodiscard]] virtual std::uint64_t size() const = 0;

    /// The whole content of the file. Journals are a few tens of kilobytes a day.
    [[nodiscard]] virtual Result<std::string, StorageError> read_all() = 0;

    /// Writes at the end. The bytes are handed to the operating system but are not durable until `sync`.
    [[nodiscard]] virtual Result<void, StorageError> append(std::string_view bytes) = 0;

    /// Makes everything appended so far durable: `fsync`, `F_FULLFSYNC` or `FlushFileBuffers`, not `QFile::flush`.
    [[nodiscard]] virtual Result<void, StorageError> sync() = 0;

    /// Cuts the file to `length` bytes. The caller syncs.
    [[nodiscard]] virtual Result<void, StorageError> truncate(std::uint64_t length) = 0;
};

/// Opens (creating when needed) a journal file for appending and takes the exclusive lock of section 2.5: another
/// writer on the same file is refused with `LOCKED`, and so is a second `open_journal_file` in the same process.
[[nodiscard]] Result<std::unique_ptr<JournalFile>, StorageError> open_journal_file(const std::filesystem::path& path);

}  // namespace ej
