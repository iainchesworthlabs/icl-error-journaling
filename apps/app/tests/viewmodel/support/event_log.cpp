// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/event_log.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace ej::test
{

std::string_view event_kind_name(EventKind kind)
{
    switch (kind)
    {
    case EventKind::COMMIT:
        return "COMMIT";
    case EventKind::HAPTIC:
        return "HAPTIC";
    case EventKind::CONFIRMATION:
        return "CONFIRMATION";
    case EventKind::TOAST:
        return "TOAST";
    case EventKind::SHARE:
        return "SHARE";
    case EventKind::CLIPBOARD:
        return "CLIPBOARD";
    case EventKind::WRITE_FAILED:
        return "WRITE_FAILED";
    case EventKind::LOG_CHANGE:
        return "LOG_CHANGE";
    case EventKind::CONFIGURATION:
        return "CONFIGURATION";
    case EventKind::MARK:
        return "MARK";
    }
    return "UNKNOWN";
}

void EventLog::record(EventKind kind, std::string detail)
{
    const std::lock_guard lock(m_Mutex);
    m_Events.push_back(Event{kind, std::move(detail)});
}

std::vector<Event> EventLog::events() const
{
    const std::lock_guard lock(m_Mutex);
    return m_Events;
}

std::size_t EventLog::size() const
{
    const std::lock_guard lock(m_Mutex);
    return m_Events.size();
}

std::size_t EventLog::count(EventKind kind) const
{
    const std::lock_guard lock(m_Mutex);
    return static_cast<std::size_t>(
        std::ranges::count_if(m_Events, [kind](const Event& event) { return event.m_Kind == kind; }));
}

std::size_t EventLog::count(EventKind kind, std::string_view detail) const
{
    const std::lock_guard lock(m_Mutex);
    return static_cast<std::size_t>(std::ranges::count_if(
        m_Events, [&](const Event& event) { return event.m_Kind == kind && event.m_Detail == detail; }));
}

std::vector<std::string> EventLog::details(EventKind kind) const
{
    const std::lock_guard lock(m_Mutex);
    std::vector<std::string> result;
    for (const Event& event : m_Events)
    {
        if (event.m_Kind == kind)
        {
            result.push_back(event.m_Detail);
        }
    }
    return result;
}

std::optional<std::size_t> EventLog::first_index(EventKind kind, std::string_view detail) const
{
    const std::lock_guard lock(m_Mutex);
    for (std::size_t i = 0; i < m_Events.size(); ++i)
    {
        if (m_Events[i].m_Kind == kind && m_Events[i].m_Detail == detail)
        {
            return i;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> EventLog::last_index(EventKind kind, std::string_view detail) const
{
    const std::lock_guard lock(m_Mutex);
    for (std::size_t i = m_Events.size(); i > 0; --i)
    {
        if (m_Events[i - 1].m_Kind == kind && m_Events[i - 1].m_Detail == detail)
        {
            return i - 1;
        }
    }
    return std::nullopt;
}

bool EventLog::occurred_before(const Event& before, const Event& after) const
{
    const auto first = first_index(before.m_Kind, before.m_Detail);
    const auto second = first_index(after.m_Kind, after.m_Detail);
    return first && second && *first < *second;
}

std::string EventLog::format() const
{
    const std::lock_guard lock(m_Mutex);
    std::ostringstream out;
    for (std::size_t i = 0; i < m_Events.size(); ++i)
    {
        out << i << ' ' << event_kind_name(m_Events[i].m_Kind);
        if (!m_Events[i].m_Detail.empty())
        {
            out << ' ' << m_Events[i].m_Detail;
        }
        out << '\n';
    }
    return out.str();
}

void EventLog::clear()
{
    const std::lock_guard lock(m_Mutex);
    m_Events.clear();
}

} // namespace ej::test
