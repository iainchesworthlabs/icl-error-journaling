// SPDX-License-Identifier: GPL-3.0-or-later
#include "../durable_sync.hpp"

#include "../errno_error.hpp"

#include <io.h>
#include <share.h>
#include <windows.h>

namespace ej::platform
{

namespace
{

StorageError from_last_error(std::string what)
{
    const DWORD code = ::GetLastError();
    StorageErrorKind kind = StorageErrorKind::IO;
    if (code == ERROR_DISK_FULL || code == ERROR_HANDLE_DISK_FULL)
    {
        kind = StorageErrorKind::DISK_FULL;
    }
    else if (code == ERROR_ACCESS_DENIED || code == ERROR_WRITE_PROTECT)
    {
        kind = StorageErrorKind::PERMISSION;
    }
    return make_error(kind, std::move(what), static_cast<int>(code));
}

}  // namespace

Result<FilePtr, StorageError> open_for_append(const std::filesystem::path& path)
{
    // _SH_DENYNO: the CRT's default for fopen would keep other handles, a reader's among them, from opening the file.
    std::FILE* file = ::_wfsopen(path.c_str(), L"ab", _SH_DENYNO);
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
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file)));
    if (handle == INVALID_HANDLE_VALUE || ::FlushFileBuffers(handle) == 0)
    {
        return fail(from_last_error("a journal file could not be synced"));
    }
    return {};
}

Result<void, StorageError> sync_directory(const std::filesystem::path& directory)
{
    // NTFS journals changes to directories, and a directory can often not be opened for flushing; this is best effort.
    const HANDLE handle = ::CreateFileW(directory.c_str(), GENERIC_READ | GENERIC_WRITE,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle != INVALID_HANDLE_VALUE)
    {
        (void)::FlushFileBuffers(handle);
        ::CloseHandle(handle);
    }
    return {};
}

Result<void, StorageError> truncate_file(std::FILE* file, std::uint64_t length)
{
    if (std::fflush(file) != 0)
    {
        return fail(error_from_errno("a journal file could not be flushed", errno));
    }
    const errno_t code = ::_chsize_s(::_fileno(file), static_cast<long long>(length));
    if (code != 0)
    {
        return fail(error_from_errno("a journal file could not be truncated", code));
    }
    return {};
}

}  // namespace ej::platform
