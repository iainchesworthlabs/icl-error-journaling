// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/android_system_plan.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

namespace ej::bridge
{

namespace
{

// The smallest and largest iOS text scales (accessibility_settings.hpp): xSmall 14 pt and accessibility 5 at 53 pt, over 17.
constexpr double smallest_text_scale = 0.82;
constexpr double largest_text_scale = 3.12;

} // namespace

const char* android_mark_name(Mark mark) noexcept
{
    switch (mark)
    {
    case Mark::PROCESS_START:
        return "ProcessStart";
    case Mark::STORAGE_READY:
        return "StorageReady";
    case Mark::QML_LOADED:
        return "QmlLoaded";
    case Mark::FIRST_FRAME:
        return "FirstFrame";
    case Mark::BOARD_USABLE:
        return "BoardUsable";
    case Mark::TAP_TO_HAPTIC:
        return "TapToHaptic";
    case Mark::TOUCH_AGE:
        return "TouchAge";
    case Mark::TAP_TO_COMMIT:
        return "TapToCommit";
    }
    return "Unknown";
}

std::optional<std::uint64_t> proc_stat_start_ticks(std::string_view stat) noexcept
{
    const auto close = stat.rfind(')');
    if (close == std::string_view::npos)
    {
        return std::nullopt;
    }
    // After "pid (name)" the fields are state (3), ppid (4) and so on: field 22, starttime, is the 20th of what is left.
    std::string_view rest = stat.substr(close + 1);
    constexpr int starttime_index = 19; // zero-based among the fields after the name
    for (int index = 0;; ++index)
    {
        const auto begin = rest.find_first_not_of(' ');
        if (begin == std::string_view::npos)
        {
            return std::nullopt;
        }
        rest.remove_prefix(begin);
        const auto end = std::min(rest.find_first_of(" \n"), rest.size());
        const std::string_view field = rest.substr(0, end);
        if (index == starttime_index)
        {
            std::uint64_t ticks = 0;
            const auto parsed = std::from_chars(field.data(), field.data() + field.size(), ticks);
            if (parsed.ec != std::errc{} || parsed.ptr != field.data() + field.size())
            {
                return std::nullopt;
            }
            return ticks;
        }
        rest.remove_prefix(end);
    }
}

double android_text_scale(double font_scale) noexcept
{
    if (!std::isfinite(font_scale) || font_scale <= 0.0)
    {
        return 1.0;
    }
    const double clamped = std::clamp(font_scale, smallest_text_scale, largest_text_scale);
    return std::round(clamped * 100.0) / 100.0;
}

bool android_reduce_motion(double animator_scale) noexcept
{
    return !(animator_scale > 0.0); // 0, a negative number or not a number: no animation
}

} // namespace ej::bridge
