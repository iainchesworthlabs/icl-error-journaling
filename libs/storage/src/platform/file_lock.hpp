// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstdio>

namespace ej::platform
{

/// Takes an exclusive lock on the file for as long as it stays open, without waiting. A second writer is refused
/// (`LOCKED`), not interleaved (section 2.5). The lock does not stop other handles from reading the file, which
/// recovery does while the app is not running and tests do while a writer is.
[[nodiscard]] Result<void, StorageError> lock_exclusive(std::FILE* file);

}  // namespace ej::platform
