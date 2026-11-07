// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/storage_error.hpp"

#include <cerrno>
#include <string>
#include <utility>

namespace ej::platform
{

/// A storage error from a C library error number: a full disk, a refused permission, or any other failure of the file
/// system. The message names what was being done and holds no file content.
[[nodiscard]] inline StorageError error_from_errno(std::string what, int code)
{
    StorageErrorKind kind = StorageErrorKind::IO;
    if (code == ENOSPC)
    {
        kind = StorageErrorKind::DISK_FULL;
    }
    else if (code == EACCES || code == EPERM || code == EROFS)
    {
        kind = StorageErrorKind::PERMISSION;
    }
    return make_error(kind, std::move(what), code);
}

}  // namespace ej::platform
