// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>
#include <system_error>

namespace ej::test
{

/// A fresh directory under the test's working directory (the build tree, never the system temp folder, which may be on
/// a nearly full drive), removed when the object goes away.
class TempDir
{
public:
    TempDir()
    {
        static std::atomic<unsigned> counter{0};
        std::random_device device;
        m_Path = std::filesystem::current_path() / "ej-test-tmp" /
                 (std::to_string(device()) + "-" + std::to_string(++counter));
        std::filesystem::create_directories(m_Path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    ~TempDir()
    {
        std::error_code ignored;
        std::filesystem::remove_all(m_Path, ignored);
    }

    [[nodiscard]] const std::filesystem::path& path() const { return m_Path; }

private:
    std::filesystem::path m_Path;
};

/// Every regular file under `directory` with its bytes, by relative path: to show that a call left the files as they
/// were.
[[nodiscard]] inline std::map<std::string, std::string> snapshot_of(const std::filesystem::path& directory)
{
    std::map<std::string, std::string> files;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(directory))
    {
        if (entry.is_regular_file())
        {
            std::ifstream in(entry.path(), std::ios::binary);
            files[std::filesystem::relative(entry.path(), directory).generic_string()] =
                std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        }
    }
    return files;
}

}  // namespace ej::test
