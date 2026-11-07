// SPDX-License-Identifier: GPL-3.0-or-later
#include "../unreadable_file.hpp"

#include <share.h>

#include <cstdio>
#include <fstream>

namespace ej::test
{

struct UnreadableFile::Impl
{
    std::FILE* m_File = nullptr;
    bool m_Works = false;
};

UnreadableFile::UnreadableFile(const std::filesystem::path& path) : m_Impl(std::make_unique<Impl>())
{
    // Open without sharing: nothing else can open the file while this handle is held, as when another process has it.
    m_Impl->m_File = ::_wfsopen(path.c_str(), L"rb", _SH_DENYRW);
    m_Impl->m_Works = m_Impl->m_File != nullptr && !std::ifstream(path, std::ios::binary);
}

UnreadableFile::~UnreadableFile()
{
    if (m_Impl->m_File != nullptr)
    {
        std::fclose(m_Impl->m_File);
    }
}

bool UnreadableFile::works() const
{
    return m_Impl->m_Works;
}

}  // namespace ej::test
