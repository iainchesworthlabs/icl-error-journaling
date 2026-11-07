// SPDX-License-Identifier: GPL-3.0-or-later
#include "../unreadable_file.hpp"

#include <fstream>
#include <system_error>

namespace ej::test
{

struct UnreadableFile::Impl
{
    std::filesystem::path m_Path;
    std::filesystem::perms m_Before = std::filesystem::perms::none;
    bool m_Works = false;
};

UnreadableFile::UnreadableFile(const std::filesystem::path& path) : m_Impl(std::make_unique<Impl>())
{
    std::error_code code;
    m_Impl->m_Path = path;
    m_Impl->m_Before = std::filesystem::status(path, code).permissions();
    std::filesystem::permissions(path, std::filesystem::perms::none, code);
    // As root, permissions mean nothing: the file can still be read, and the case cannot be made.
    m_Impl->m_Works = !std::ifstream(path, std::ios::binary);
}

UnreadableFile::~UnreadableFile()
{
    std::error_code code;
    std::filesystem::permissions(m_Impl->m_Path, m_Impl->m_Before, code);
}

bool UnreadableFile::works() const
{
    return m_Impl->m_Works;
}

}  // namespace ej::test
