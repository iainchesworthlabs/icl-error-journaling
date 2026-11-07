// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage_location.hpp"

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
    const std::array<std::filesystem::path, 4> folders{archived_journal_dir(), backup_dir(), quarantine_dir(), m_Root};
    for (const auto& folder : folders)
    {
        std::error_code code;
        std::filesystem::create_directories(folder, code);
        if (code)
        {
            return fail(make_error(kind_of(code), "a storage folder could not be created", code.value()));
        }
    }
    return {};
}

}  // namespace ej
