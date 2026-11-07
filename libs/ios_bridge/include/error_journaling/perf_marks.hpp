// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>
#include <string_view>

namespace ej::bridge
{

/// The instants and intervals the exit gate is measured by (Stage 5, "Timing marks and the device build").
enum class Mark
{
    PROCESS_START, ///< main() was reached; the value is the time since the process was created.
    STORAGE_READY,
    QML_LOADED,
    FIRST_FRAME,
    BOARD_USABLE,  ///< The first frame with storage ready and the board enabled: the cold-start figure.
    TAP_TO_HAPTIC, ///< From the press handler to the haptic call returning.
    TOUCH_AGE,     ///< From the touch's event timestamp to the press handler.
    TAP_TO_COMMIT, ///< From the press handler to the acknowledged commit.
};

/// Tells Instruments (and the console) when something happened. Timings only: no content, no network, no analytics
/// (NFR-09). Every call is made on the main thread.
class PerfMarks
{
public:
    PerfMarks() = default;
    PerfMarks(const PerfMarks&) = delete;
    PerfMarks& operator=(const PerfMarks&) = delete;
    virtual ~PerfMarks() = default;

    /// An interval starts and ends. One interval of a given mark is open at a time.
    virtual void begin(Mark mark) = 0;
    virtual void end(Mark mark) = 0;

    /// An instant, with a value in milliseconds.
    virtual void event(Mark mark, double milliseconds) = 0;

    /// Milliseconds since the process was created (as Apple's launch measurements count it).
    [[nodiscard]] virtual double ms_since_process_start() = 0;

    /// Milliseconds on the clock that touch timestamps use (the system uptime on iOS).
    [[nodiscard]] virtual double uptime_ms() = 0;

    /// One line to the system log, readable in Console from any build. The caller keeps content out of it.
    virtual void log(std::string_view line) = 0;
};

/// ios/perf_marks.mm (os_signpost, Points of Interest) or desktop/perf_marks.cpp (records nothing).
[[nodiscard]] std::unique_ptr<PerfMarks> make_perf_marks();

} // namespace ej::bridge
