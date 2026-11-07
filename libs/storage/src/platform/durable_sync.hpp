// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>

/// The pieces of file handling that differ by operating system (section 2.1 of the Stage 2 plan). CMake compiles the
/// definitions in `src/platform/<os>/`; nothing else in the library asks which system it runs on.
namespace ej::platform
{

struct FileCloser
{
    void operator()(std::FILE* file) const noexcept
    {
        if (file != nullptr)
        {
            std::fclose(file);
        }
    }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;

/// Opens `path` for appending, creating it when it does not exist. Writes always go to the end of the file.
[[nodiscard]] Result<FilePtr, StorageError> open_for_append(const std::filesystem::path& path);

/// Flushes the stdio buffer and makes what was written durable, which is more than `QFile::flush` or `fflush` do:
/// `fsync` on Linux, `F_FULLFSYNC` on macOS and iOS (where `fsync` does not flush the drive's cache), and
/// `FlushFileBuffers` on Windows.
[[nodiscard]] Result<void, StorageError> durable_sync(std::FILE* file);

/// Makes the directory entries of `directory` durable, so that a file just created in it, or renamed into it, is still
/// there after a power loss. Best effort on Windows, where NTFS journals directory changes and a directory may not be
/// opened for flushing.
[[nodiscard]] Result<void, StorageError> sync_directory(const std::filesystem::path& directory);

/// Cuts the file to `length` bytes (flushing the stdio buffer first). The caller syncs.
[[nodiscard]] Result<void, StorageError> truncate_file(std::FILE* file, std::uint64_t length);

}  // namespace ej::platform
