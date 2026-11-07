// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/perf_marks.hpp"

#include <functional>
#include <utility>

namespace ej::bridge
{

/// Decides when the board is usable and says so once (NFR-04). The board is usable at the first frame swapped after
/// storage is ready, QML is loaded and the board is enabled; it is never usable earlier and is reported once. Each mark
/// is emitted once, with the time since the process was created, and when the board becomes usable one line goes to the
/// log: `launch: usable at 812 ms (storage 140, qml 380, first frame 610)`. The line holds timings only.
///
/// It is called from main.cpp (process start, QML loaded, frames) and from whoever opens storage and enables the board.
/// Main thread only.
class LaunchTracker
{
public:
    explicit LaunchTracker(PerfMarks& marks);

    void process_started();
    void storage_ready();
    void qml_loaded();
    void board_enabled();
    /// The window swapped a frame. The first one is FIRST_FRAME; the first one with everything else ready is
    /// BOARD_USABLE; later ones do nothing.
    void frame_swapped();

    /// Called once with the milliseconds since the process was created, when the board becomes usable (M1).
    void set_usable_handler(std::function<void(double)> handler)
    {
        m_OnUsable = std::move(handler);
    }

    [[nodiscard]] bool usable() const noexcept
    {
        return m_Usable;
    }

private:
    void maybe_usable();

    PerfMarks& m_Marks;
    std::function<void(double)> m_OnUsable;
    bool m_StorageReady = false;
    bool m_QmlLoaded = false;
    bool m_BoardEnabled = false;
    bool m_FirstFrame = false;
    bool m_Usable = false;
    double m_StorageMs = 0;
    double m_QmlMs = 0;
    double m_FirstFrameMs = 0;
};

} // namespace ej::bridge
