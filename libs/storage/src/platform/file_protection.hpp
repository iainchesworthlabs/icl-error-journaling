// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <filesystem>

namespace ej::platform
{

/// The data protection classes the store tells apart (NFR-09). Anything the platform reports that is not one of the
/// first four is `OTHER`.
enum class ProtectionClass
{
    NONE,
    COMPLETE,
    COMPLETE_UNLESS_OPEN,
    COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION,
    OTHER,
    UNSUPPORTED  // the platform has no per-file protection (a desktop)
};

/// Gives a file or folder the protection class the platform offers for data at rest, and reads it back. The portable
/// code decides which paths to protect (`protect.hpp`); this is the only part that talks to the operating system, and
/// the tests replace it with a fake.
class FileProtection
{
public:
    virtual ~FileProtection() = default;

    [[nodiscard]] virtual Result<void, StorageError> apply(const std::filesystem::path& path, ProtectionClass wanted) = 0;
    [[nodiscard]] virtual Result<ProtectionClass, StorageError> read_back(const std::filesystem::path& path) = 0;
};

/// The operating system's implementation: `NSFileProtectionKey` on iOS, a stub elsewhere that applies nothing and
/// reads back `UNSUPPORTED`.
[[nodiscard]] FileProtection& system_file_protection();

}  // namespace ej::platform
