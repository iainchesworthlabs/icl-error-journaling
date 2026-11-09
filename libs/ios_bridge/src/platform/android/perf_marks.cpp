// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 9: until package 9.6 has an Android implementation of this capability it is the desktop stub (a copy of
// ../desktop/perf_marks.cpp). check_platform_layout.py wants one file per capability in every platform directory.
#include "error_journaling/perf_marks.hpp"

#include <chrono>

namespace ej::bridge
{

namespace
{

using SteadyClock = std::chrono::steady_clock;

class DesktopPerfMarks final : public PerfMarks
{
public:
    void begin(Mark /*mark*/) override {}
    void end(Mark /*mark*/) override {}
    void event(Mark /*mark*/, double /*milliseconds*/) override {}

    [[nodiscard]] double ms_since_process_start() override
    {
        // The time since this object was made, which is as early as the stub can say.
        return std::chrono::duration<double, std::milli>(SteadyClock::now() - m_Start).count();
    }

    [[nodiscard]] double uptime_ms() override
    {
        return std::chrono::duration<double, std::milli>(SteadyClock::now().time_since_epoch()).count();
    }

    void log(std::string_view /*line*/) override {}

private:
    SteadyClock::time_point m_Start = SteadyClock::now();
};

} // namespace

std::unique_ptr<PerfMarks> make_perf_marks()
{
    return std::make_unique<DesktopPerfMarks>();
}

} // namespace ej::bridge
