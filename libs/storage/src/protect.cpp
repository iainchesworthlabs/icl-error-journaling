// SPDX-License-Identifier: GPL-3.0-or-later
#include "protect.hpp"

#include <atomic>
#include <cstdio>
#include <mutex>
#include <system_error>

namespace ej
{

namespace
{

constexpr std::size_t max_warnings = 100;
constexpr auto wanted_class = platform::ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION;

std::atomic<platform::FileProtection*> g_Override{nullptr};
std::mutex g_WarningsMutex;
std::vector<std::string> g_Warnings;

platform::FileProtection& current()
{
    platform::FileProtection* replacement = g_Override.load();
    return replacement != nullptr ? *replacement : platform::system_file_protection();
}

void warn(const std::filesystem::path& path, const std::string& what)
{
    const std::string line = "protection: " + what + ": " + path.generic_string();
    std::fprintf(stderr, "%s\n", line.c_str());
    const std::lock_guard lock(g_WarningsMutex);
    if (g_Warnings.size() < max_warnings)
    {
        g_Warnings.push_back(line);
    }
}

}  // namespace

void protect_path(const std::filesystem::path& path)
{
    std::error_code code;
    if (!std::filesystem::exists(path, code))
    {
        return;
    }
    platform::FileProtection& protection = current();
    if (auto applied = protection.apply(path, wanted_class); !applied)
    {
        warn(path, "could not be applied (" + describe(applied.error()) + ")");
        return;
    }
    const auto found = protection.read_back(path);
    if (!found)
    {
        warn(path, "could not be read back (" + describe(found.error()) + ")");
    }
    else if (*found != wanted_class && *found != platform::ProtectionClass::UNSUPPORTED)
    {
        warn(path, "read back as a different class");
    }
}

void protect_database_files(const std::filesystem::path& database)
{
    for (const char* suffix : {"", "-wal", "-shm"})
    {
        std::filesystem::path file = database;
        file += suffix;
        protect_path(file);
    }
}

void protect_store(const StorageLocation& location)
{
    std::error_code code;
    // The data folder is protected last when it has to be swept, so a launch that was cut short sweeps again.
    const auto root_state = current().read_back(location.root());
    const bool swept = root_state && *root_state == wanted_class;

    for (const auto& folder : {location.journal_dir(), location.archived_journal_dir(), location.backup_dir(),
                               location.quarantine_dir()})
    {
        protect_path(folder);
    }
    protect_database_files(location.database_path());
    for (const auto& entry : std::filesystem::directory_iterator(location.journal_dir(), code))
    {
        protect_path(entry.path());
    }
    if (!swept)
    {
        // Files made before protection existed, in the journal archive, the backups and the quarantine: once.
        for (const auto& entry : std::filesystem::recursive_directory_iterator(location.root(), code))
        {
            protect_path(entry.path());
        }
    }
    protect_path(location.root());
}

std::vector<std::string> protection_warnings()
{
    const std::lock_guard lock(g_WarningsMutex);
    return g_Warnings;
}

ScopedFileProtection::ScopedFileProtection(platform::FileProtection& replacement)
    : m_Previous(g_Override.exchange(&replacement))
{
}

ScopedFileProtection::~ScopedFileProtection()
{
    g_Override.store(m_Previous);
}

}  // namespace ej
