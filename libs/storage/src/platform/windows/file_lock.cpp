// SPDX-License-Identifier: GPL-3.0-or-later
#include "../file_lock.hpp"

#include <io.h>
#include <windows.h>

namespace ej::platform
{

Result<void, StorageError> lock_exclusive(std::FILE* file)
{
    const auto handle = reinterpret_cast<HANDLE>(::_get_osfhandle(::_fileno(file)));
    // One byte far beyond the end of the file. Windows locks are mandatory for the locked bytes, so locking the
    // contents would stop other handles from reading the journal; locking a byte nobody reads makes the lock a signal
    // only.
    OVERLAPPED where{};
    where.Offset = 0;
    where.OffsetHigh = 0x7FFFFFFFU;
    if (handle == INVALID_HANDLE_VALUE ||
        ::LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &where) == 0)
    {
        const DWORD code = ::GetLastError();
        const bool held = code == ERROR_LOCK_VIOLATION || code == ERROR_IO_PENDING;
        return fail(make_error(held ? StorageErrorKind::LOCKED : StorageErrorKind::IO,
                               held ? "a journal file is open in another writer" : "a journal file could not be locked",
                               static_cast<int>(code)));
    }
    return {};
}

}  // namespace ej::platform
