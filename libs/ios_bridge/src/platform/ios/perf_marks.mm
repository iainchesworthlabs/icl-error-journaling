// SPDX-License-Identifier: GPL-3.0-or-later
#import <Foundation/Foundation.h>
#import <os/log.h>
#import <os/signpost.h>

#include "error_journaling/perf_marks.hpp"

#include <sys/sysctl.h>
#include <sys/time.h>
#include <unistd.h>

#include <string>

namespace ej::bridge
{

namespace
{

// The subsystem is the bundle identifier, so the marks show in Instruments' Points of Interest with no configuration.
const char* subsystem()
{
    static NSString* const identifier =
        NSBundle.mainBundle.bundleIdentifier != nil ? NSBundle.mainBundle.bundleIdentifier
                                                    : @"com.iainchesworthlabs.fieldtest";
    return identifier.UTF8String;
}

// The signpost names have to be literals, so each mark has its own line in these switches.
class IosPerfMarks final : public PerfMarks
{
public:
    IosPerfMarks()
        : m_Points(os_log_create(subsystem(), OS_LOG_CATEGORY_POINTS_OF_INTEREST))
        , m_Console(os_log_create(subsystem(), "launch"))
        , m_ProcessStart(process_start_seconds())
    {
    }

    void begin(Mark mark) override
    {
        switch (mark)
        {
        case Mark::TAP_TO_HAPTIC:
            os_signpost_interval_begin(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "TapToHaptic");
            break;
        case Mark::TAP_TO_COMMIT:
            os_signpost_interval_begin(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "TapToCommit");
            break;
        default:
            break;  // the other marks are instants
        }
    }

    void end(Mark mark) override
    {
        switch (mark)
        {
        case Mark::TAP_TO_HAPTIC:
            os_signpost_interval_end(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "TapToHaptic");
            break;
        case Mark::TAP_TO_COMMIT:
            os_signpost_interval_end(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "TapToCommit");
            break;
        default:
            break;
        }
    }

    void event(Mark mark, double milliseconds) override
    {
        switch (mark)
        {
        case Mark::PROCESS_START:
            os_signpost_event_emit(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "ProcessStart", "%{public}.1f ms", milliseconds);
            break;
        case Mark::STORAGE_READY:
            os_signpost_event_emit(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "StorageReady", "%{public}.1f ms", milliseconds);
            break;
        case Mark::QML_LOADED:
            os_signpost_event_emit(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "QmlLoaded", "%{public}.1f ms", milliseconds);
            break;
        case Mark::FIRST_FRAME:
            os_signpost_event_emit(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "FirstFrame", "%{public}.1f ms", milliseconds);
            break;
        case Mark::BOARD_USABLE:
            os_signpost_event_emit(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "BoardUsable", "%{public}.1f ms", milliseconds);
            break;
        case Mark::TOUCH_AGE:
            os_signpost_event_emit(m_Points, OS_SIGNPOST_ID_EXCLUSIVE, "TouchAge", "%{public}.1f ms", milliseconds);
            break;
        case Mark::TAP_TO_HAPTIC:
        case Mark::TAP_TO_COMMIT:
            break;  // intervals
        }
    }

    [[nodiscard]] double ms_since_process_start() override
    {
        struct timeval now{};
        gettimeofday(&now, nullptr);
        const double seconds = static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_usec) / 1e6;
        return (seconds - m_ProcessStart) * 1000.0;
    }

    [[nodiscard]] double uptime_ms() override
    {
        // The time base of UITouch.timestamp, which Qt passes on as the event timestamp (assumption A5).
        return NSProcessInfo.processInfo.systemUptime * 1000.0;
    }

    void log(std::string_view line) override
    {
        const std::string text(line);
        os_log(m_Console, "%{public}s", text.c_str());
    }

private:
    // When the process was created, in seconds since 1970, from the kernel; the time main() ran if the call fails.
    static double process_start_seconds()
    {
        struct kinfo_proc info{};
        size_t size = sizeof(info);
        int mib[4] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
        if (sysctl(mib, 4, &info, &size, nullptr, 0) == 0 && size == sizeof(info))
        {
            const struct timeval start = info.kp_proc.p_un.__p_starttime;
            return static_cast<double>(start.tv_sec) + static_cast<double>(start.tv_usec) / 1e6;
        }
        struct timeval now{};
        gettimeofday(&now, nullptr);
        return static_cast<double>(now.tv_sec) + static_cast<double>(now.tv_usec) / 1e6;
    }

    os_log_t m_Points;
    os_log_t m_Console;
    double m_ProcessStart;
};

}  // namespace

std::unique_ptr<PerfMarks> make_perf_marks()
{
    return std::make_unique<IosPerfMarks>();
}

}  // namespace ej::bridge
