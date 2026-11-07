// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <filesystem>

namespace ej::platform
{

/// Gives a file or folder of the store the protection the platform offers for data at rest (NFR-09). Nothing to do on a
/// desktop. The iOS definition applies `NSFileProtectionCompleteUntilFirstUnlock` and lands in Stage 5, with the
/// requirements in section 2.10 of the Stage 2 plan: the data folder, the database and its `-wal` and `-shm` files, the
/// journal, `journal/archive/`, `backup/` and `quarantine/`.
[[nodiscard]] Result<void, StorageError> apply_file_protection(const std::filesystem::path& path);

}  // namespace ej::platform
