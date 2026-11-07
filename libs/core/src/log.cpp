// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/log.hpp"

#include <algorithm>

namespace ej
{

Result<void, Log::Error> Log::add(Entry entry)
{
    const bool duplicate = std::any_of(m_Entries.begin(), m_Entries.end(),
                                       [&entry](const Entry& existing) { return existing.m_Id == entry.m_Id; });
    if (duplicate)
    {
        return fail(Error::DUPLICATE_ID);
    }
    m_Entries.push_back(std::move(entry));
    return {};
}

}  // namespace ej
