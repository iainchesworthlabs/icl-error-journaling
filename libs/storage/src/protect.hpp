// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/storage_location.hpp"
#include "platform/file_protection.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace ej
{

/// Gives a file or folder of the store the complete-until-first-user-authentication class and reads the class back
/// (NFR-09). A failure of either is logged and kept in `protection_warnings()`; it is never returned, because refusing
/// to open the store over a privacy hardening would put the open log at risk. A path that does not exist is skipped.
void protect_path(const std::filesystem::path& path);

/// The database and, when they exist, its `-wal` and `-shm` files.
void protect_database_files(const std::filesystem::path& database);

/// What a launch protects: the folders, the database and its companions, and the journals of `journal/`. A data folder that does not
/// read back as protected has not been swept yet: every other file under it (the journal archive, backups, quarantine)
/// is protected too, which covers files made before protection existed, and the folder itself is protected last. Later
/// launches do not sweep, so their cost does not grow with the history.
void protect_store(const StorageLocation& location);

/// The warnings so far, one line each, with paths and error codes and never content, for the diagnostics text. At most
/// the first 100 are kept.
[[nodiscard]] std::vector<std::string> protection_warnings();

/// Replaces the implementation used by `protect_path` for the life of the object. For tests.
class ScopedFileProtection
{
public:
    explicit ScopedFileProtection(platform::FileProtection& replacement);
    ~ScopedFileProtection();
    ScopedFileProtection(const ScopedFileProtection&) = delete;
    ScopedFileProtection& operator=(const ScopedFileProtection&) = delete;

private:
    platform::FileProtection* m_Previous;
};

}  // namespace ej
