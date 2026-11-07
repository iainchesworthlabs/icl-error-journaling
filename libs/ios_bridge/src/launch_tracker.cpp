// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/launch_tracker.hpp"

#include <string>

namespace ej::bridge
{

LaunchTracker::LaunchTracker(PerfMarks& marks) : m_Marks(marks) {}

void LaunchTracker::process_started()
{
    m_Marks.event(Mark::PROCESS_START, m_Marks.ms_since_process_start());
}

void LaunchTracker::storage_ready()
{
    if (m_StorageReady)
    {
        return;
    }
    m_StorageReady = true;
    m_StorageMs = m_Marks.ms_since_process_start();
    m_Marks.event(Mark::STORAGE_READY, m_StorageMs);
}

void LaunchTracker::qml_loaded()
{
    if (m_QmlLoaded)
    {
        return;
    }
    m_QmlLoaded = true;
    m_QmlMs = m_Marks.ms_since_process_start();
    m_Marks.event(Mark::QML_LOADED, m_QmlMs);
}

void LaunchTracker::board_enabled()
{
    m_BoardEnabled = true;
}

void LaunchTracker::frame_swapped()
{
    if (!m_FirstFrame)
    {
        m_FirstFrame = true;
        m_FirstFrameMs = m_Marks.ms_since_process_start();
        m_Marks.event(Mark::FIRST_FRAME, m_FirstFrameMs);
    }
    maybe_usable();
}

void LaunchTracker::maybe_usable()
{
    if (m_Usable || !m_FirstFrame || !m_StorageReady || !m_QmlLoaded || !m_BoardEnabled)
    {
        return;
    }
    m_Usable = true;
    const double now = m_Marks.ms_since_process_start();
    m_Marks.event(Mark::BOARD_USABLE, now);
    if (m_OnUsable)
    {
        m_OnUsable(now);
    }
    const auto whole = [](double ms) { return std::to_string(static_cast<long long>(ms + 0.5)); };
    m_Marks.log("launch: usable at " + whole(now) + " ms (storage " + whole(m_StorageMs) + ", qml " + whole(m_QmlMs) +
                ", first frame " + whole(m_FirstFrameMs) + ")");
}

} // namespace ej::bridge
