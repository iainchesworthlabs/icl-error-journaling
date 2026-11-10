// SPDX-License-Identifier: GPL-3.0-or-later
// Android timing marks (docs/plan/stage-9.md, "Timing marks"). Intervals and instants go to the system trace, where
// Perfetto and Android Studio's profiler show them (ATrace); a line goes to logcat under the tag "ej", readable from any
// build. Timings only: no content, no network, no analytics (NFR-09). The decisions (the names, the start time from
// /proc/self/stat) are in src/android_system_plan.cpp, which a test covers on the desktop.
#include "error_journaling/android_system_plan.hpp"
#include "error_journaling/perf_marks.hpp"

#include <android/log.h>
#include <android/trace.h>

#include <time.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace ej::bridge
{

namespace
{

double now_ms(clockid_t clock)
{
    timespec time{};
    clock_gettime(clock, &time);
    return static_cast<double>(time.tv_sec) * 1000.0 + static_cast<double>(time.tv_nsec) / 1.0e6;
}

class AndroidPerfMarks final : public PerfMarks
{
public:
    AndroidPerfMarks() : m_Constructed(now_ms(CLOCK_BOOTTIME)), m_StartBoottimeMs(read_process_start_ms())
    {
    }

    // ATrace sections nest on one thread; the marks begin and end in that order (a tap's commit interval holds its haptic
    // interval), and a trace that shows them a little off is a diagnostic's worst case.
    void begin(Mark mark) override
    {
        ATrace_beginSection(android_mark_name(mark));
    }

    void end(Mark /*mark*/) override
    {
        ATrace_endSection();
    }

    void event(Mark mark, double milliseconds) override
    {
        char name[64];
        std::snprintf(name, sizeof(name), "%s %.1f ms", android_mark_name(mark), milliseconds);
        ATrace_beginSection(name);
        ATrace_endSection();
    }

    [[nodiscard]] double ms_since_process_start() override
    {
        // The kernel's start time of the process (as Process.getStartElapsedRealtime reports it), on the clock that counts
        // while the phone sleeps. If /proc could not be read, the time since this object was made is as early as it gets.
        const double reference = m_StartBoottimeMs >= 0.0 ? m_StartBoottimeMs : m_Constructed;
        return now_ms(CLOCK_BOOTTIME) - reference;
    }

    [[nodiscard]] double uptime_ms() override
    {
        // The clock of MotionEvent.getEventTime(), which Qt's touch timestamps come from (assumption T1): uptime, which
        // does not count sleep. On Android that is CLOCK_MONOTONIC.
        return now_ms(CLOCK_MONOTONIC);
    }

    void log(std::string_view line) override
    {
        __android_log_print(ANDROID_LOG_INFO, "ej", "%.*s", static_cast<int>(line.size()), line.data());
    }

private:
    // When this process started, in milliseconds on CLOCK_BOOTTIME; negative when it cannot be read.
    static double read_process_start_ms()
    {
        std::ifstream stat("/proc/self/stat");
        std::stringstream text;
        text << stat.rdbuf();
        const auto ticks = proc_stat_start_ticks(text.str());
        const long per_second = sysconf(_SC_CLK_TCK);
        if (!ticks || per_second <= 0)
        {
            return -1.0;
        }
        return static_cast<double>(*ticks) * 1000.0 / static_cast<double>(per_second);
    }

    double m_Constructed;
    double m_StartBoottimeMs;
};

} // namespace

std::unique_ptr<PerfMarks> make_perf_marks()
{
    return std::make_unique<AndroidPerfMarks>();
}

} // namespace ej::bridge
