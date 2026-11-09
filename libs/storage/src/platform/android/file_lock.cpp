// SPDX-License-Identifier: GPL-3.0-or-later
// Bionic is a Linux libc: the same flock call as src/platform/linux/file_lock.cpp.
#include "../file_lock.hpp"

#include "../errno_error.hpp"

#include <sys/file.h>

namespace ej::platform
{

Result<void, StorageError> lock_exclusive(std::FILE* file)
{
    if (::flock(::fileno(file), LOCK_EX | LOCK_NB) != 0)
    {
        if (errno == EWOULDBLOCK)
        {
            return fail(make_error(StorageErrorKind::LOCKED, "a journal file is open in another writer", errno));
        }
        return fail(error_from_errno("a journal file could not be locked", errno));
    }
    return {};
}

}  // namespace ej::platform
