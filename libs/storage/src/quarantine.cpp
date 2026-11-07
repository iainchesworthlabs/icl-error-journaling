// SPDX-License-Identifier: GPL-3.0-or-later
#include "quarantine.hpp"

#include "protect.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <system_error>

namespace ej
{

namespace
{

StorageError io_error(const char* what, const std::error_code& code)
{
    const bool full = code == std::errc::no_space_on_device;
    const bool denied = code == std::errc::permission_denied || code == std::errc::read_only_file_system;
    return make_error(full     ? StorageErrorKind::DISK_FULL
                      : denied ? StorageErrorKind::PERMISSION
                               : StorageErrorKind::IO,
                      what, code.value());
}

}  // namespace

std::string Quarantine::folder_name(UtcMillis time, unsigned counter)
{
    const auto days = std::chrono::floor<std::chrono::days>(time);
    const std::chrono::year_month_day date{days};
    const std::chrono::hh_mm_ss<std::chrono::seconds> clock_time{
        std::chrono::duration_cast<std::chrono::seconds>(time - days)};
    char buffer[64];
    const int written =
        std::snprintf(buffer, sizeof buffer, "%04d%02u%02uT%02d%02d%02dZ-%u", static_cast<int>(date.year()),
                      static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()),
                      static_cast<int>(clock_time.hours().count()), static_cast<int>(clock_time.minutes().count()),
                      static_cast<int>(clock_time.seconds().count()), counter);
    return std::string(buffer, static_cast<std::size_t>(written));
}

Result<std::filesystem::path, StorageError> Quarantine::folder()
{
    if (!m_Folder.empty())
    {
        return m_Folder;
    }
    const UtcMillis now = m_Clock.now();
    std::error_code code;
    for (unsigned counter = 1; counter < 100'000; ++counter)
    {
        const std::filesystem::path candidate = m_Location.quarantine_dir() / folder_name(now, counter);
        if (std::filesystem::exists(candidate, code))
        {
            continue;
        }
        std::filesystem::create_directories(candidate, code);
        if (code)
        {
            return fail(io_error("a quarantine folder could not be created", code));
        }
        protect_path(candidate);
        m_Folder = candidate;
        return m_Folder;
    }
    return fail(make_error(StorageErrorKind::IO, "no unused quarantine folder name was found"));
}

Result<std::filesystem::path, StorageError> Quarantine::take(const std::filesystem::path& file)
{
    std::error_code code;
    if (!std::filesystem::exists(file, code))
    {
        return std::filesystem::path();
    }
    auto folder_path = folder();
    if (!folder_path)
    {
        return fail(folder_path.error());
    }
    std::filesystem::path target = *folder_path / file.filename();
    for (unsigned number = 2; std::filesystem::exists(target, code); ++number)
    {
        target = *folder_path / (file.filename().string() + "." + std::to_string(number));
    }
    std::filesystem::rename(file, target, code);
    if (code)
    {
        return fail(io_error("a file could not be moved to quarantine", code));
    }
    protect_path(target);
    m_Taken.push_back(std::filesystem::relative(target, m_Location.root(), code).generic_string());
    return target;
}

Result<std::filesystem::path, StorageError> Quarantine::take_copy(const std::filesystem::path& file)
{
    std::error_code code;
    if (!std::filesystem::exists(file, code))
    {
        return std::filesystem::path();
    }
    auto folder_path = folder();
    if (!folder_path)
    {
        return fail(folder_path.error());
    }
    std::filesystem::path target = *folder_path / file.filename();
    for (unsigned number = 2; std::filesystem::exists(target, code); ++number)
    {
        target = *folder_path / (file.filename().string() + "." + std::to_string(number));
    }
    std::filesystem::copy_file(file, target, code);
    if (code)
    {
        return fail(io_error("a file could not be copied to quarantine", code));
    }
    protect_path(target);
    m_Taken.push_back(std::filesystem::relative(target, m_Location.root(), code).generic_string());
    return target;
}

Result<void, StorageError> Quarantine::prune(std::size_t keep) const
{
    std::error_code code;
    if (!std::filesystem::is_directory(m_Location.quarantine_dir(), code))
    {
        return {};
    }
    std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> folders;
    for (const auto& entry : std::filesystem::directory_iterator(m_Location.quarantine_dir(), code))
    {
        if (entry.is_directory(code))
        {
            folders.emplace_back(entry.last_write_time(code), entry.path());
        }
    }
    if (folders.size() <= keep)
    {
        return {};
    }
    // Newest first; the oldest beyond `keep` go.
    std::sort(folders.begin(), folders.end(), [](const auto& a, const auto& b)
              { return a.first != b.first ? a.first > b.first : a.second > b.second; });
    for (std::size_t i = keep; i < folders.size(); ++i)
    {
        std::filesystem::remove_all(folders[i].second, code);
        if (code)
        {
            return fail(io_error("an old quarantine folder could not be removed", code));
        }
    }
    return {};
}

}  // namespace ej
