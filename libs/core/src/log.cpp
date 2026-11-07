// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/log.hpp"

#include <algorithm>

namespace ej
{

const Entry* Log::find(EntryId id) const
{
    const auto it = std::find_if(m_Entries.begin(), m_Entries.end(), [id](const Entry& e) { return e.m_Id == id; });
    return it == m_Entries.end() ? nullptr : &*it;
}

Entry* Log::find_mutable(EntryId id)
{
    return const_cast<Entry*>(std::as_const(*this).find(id));
}

std::vector<const Entry*> Log::sorted_entries(bool include_deleted) const
{
    std::vector<const Entry*> result;
    result.reserve(m_Entries.size());
    for (const Entry& entry : m_Entries)
    {
        if (include_deleted || !entry.m_Deleted)
        {
            result.push_back(&entry);
        }
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const Entry* a, const Entry* b) { return a->m_Captured.m_Utc < b->m_Captured.m_Utc; });
    return result;
}

Result<void, Log::Error> Log::add(Entry entry)
{
    if (find(entry.m_Id) != nullptr)
    {
        return fail(Error::DUPLICATE_ID);
    }
    m_Entries.push_back(std::move(entry));
    return {};
}

Result<void, Log::Error> Log::set_deleted(EntryId id, bool deleted)
{
    Entry* entry = find_mutable(id);
    if (entry == nullptr)
    {
        return fail(Error::UNKNOWN_ENTRY);
    }
    entry->m_Deleted = deleted;
    return {};
}

Result<void, Log::Error> Log::replace(EntryId id, Entry entry, UtcMillis edited_at)
{
    Entry* existing = find_mutable(id);
    if (existing == nullptr)
    {
        return fail(Error::UNKNOWN_ENTRY);
    }
    entry.m_Id = id;
    entry.m_Deleted = existing->m_Deleted;
    entry.m_EditedUtc = edited_at;
    if (entry.m_Captured.m_Zone.empty())
    {
        entry.m_Captured.m_Zone = existing->m_Captured.m_Zone;
    }
    *existing = std::move(entry);
    return {};
}

Result<void, Log::Error> Log::replace_body(EntryId id, EntryBody body)
{
    Entry* existing = find_mutable(id);
    if (existing == nullptr)
    {
        return fail(Error::UNKNOWN_ENTRY);
    }
    existing->m_Body = std::move(body);
    return {};
}

std::size_t Log::count_for(ErrorTypeId id) const
{
    return static_cast<std::size_t>(std::count_if(m_Entries.begin(), m_Entries.end(),
                                                  [id](const Entry& e)
                                                  {
                                                      const auto* error = std::get_if<ErrorEntry>(&e.m_Body);
                                                      return !e.m_Deleted && error != nullptr && error->m_ErrorType == id;
                                                  }));
}

}  // namespace ej
