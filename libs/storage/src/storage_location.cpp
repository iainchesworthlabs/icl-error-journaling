// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage_location.hpp"

#include "protect.hpp"

#include <array>
#include <system_error>

namespace ej
{

namespace
{

StorageErrorKind kind_of(const std::error_code& code)
{
    if (code == std::errc::permission_denied || code == std::errc::read_only_file_system)
    {
        return StorageErrorKind::PERMISSION;
    }
    if (code == std::errc::no_space_on_device)
    {
        return StorageErrorKind::DISK_FULL;
    }
    return StorageErrorKind::IO;
}

}  // namespace

Result<void, StorageError> StorageLocation::create_directories() const
{
    // A data folder that was already there belongs to an earlier version or an earlier launch; protect_store() decides
    // whether its files still need protecting and then protects the folder, last, as the sign that it is done.
    std::error_code existing;
    const bool root_existed = std::filesystem::exists(m_Root, existing);
    const std::array<std::filesystem::path, 4> folders{archived_journal_dir(), backup_dir(), quarantine_dir(), m_Root};
    for (const auto& folder : folders)
    {
        std::error_code code;
        std::filesystem::create_directories(folder, code);
        if (code)
        {
            return fail(make_error(kind_of(code), "a storage folder could not be created", code.value()));
        }
        if (folder != m_Root || !root_existed)
        {
            protect_path(folder);
        }
    }
    protect_path(journal_dir());  // made as the parent of the archive folder
    return {};
}

}  // namespace ej
