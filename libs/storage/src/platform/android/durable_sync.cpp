// SPDX-License-Identifier: GPL-3.0-or-later
// Bionic is a Linux libc: the same POSIX calls as src/platform/linux/durable_sync.cpp.
#include "../durable_sync.hpp"

#include "../errno_error.hpp"

#include <fcntl.h>
#include <unistd.h>

namespace ej::platform
{

Result<FilePtr, StorageError> open_for_append(const std::filesystem::path& path)
{
    std::FILE* file = std::fopen(path.c_str(), "ab");
    if (file == nullptr)
    {
        return fail(error_from_errno("a journal file could not be opened", errno));
    }
    return FilePtr(file);
}

Result<void, StorageError> durable_sync(std::FILE* file)
{
    if (std::fflush(file) != 0)
    {
        return fail(error_from_errno("a journal file could not be flushed", errno));
    }
    if (::fsync(::fileno(file)) != 0)
    {
        return fail(error_from_errno("a journal file could not be synced", errno));
    }
    return {};
}

Result<void, StorageError> sync_directory(const std::filesystem::path& directory)
{
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
    {
        return fail(error_from_errno("a folder could not be opened to sync it", errno));
    }
    const int synced = ::fsync(fd);
    const int code = errno;
    ::close(fd);
    if (synced != 0)
    {
        return fail(error_from_errno("a folder could not be synced", code));
    }
    return {};
}

Result<void, StorageError> truncate_file(std::FILE* file, std::uint64_t length)
{
    if (std::fflush(file) != 0 || ::ftruncate(::fileno(file), static_cast<off_t>(length)) != 0)
    {
        return fail(error_from_errno("a journal file could not be truncated", errno));
    }
    return {};
}

}  // namespace ej::platform
