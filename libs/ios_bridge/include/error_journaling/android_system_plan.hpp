// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/perf_marks.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace ej::bridge
{

/// The decisions behind the Android timing marks and accessibility settings, in portable C++ so that a test covers them on
/// the desktop and the Android code (platform/android/perf_marks.cpp, accessibility_settings.cpp) only reads the system
/// (docs/plan/stage-9.md, "Timing marks" and "Accessibility settings").

/// The name of a mark in the system trace (Perfetto) and the log. Each is a distinct, non-empty literal.
[[nodiscard]] const char* android_mark_name(Mark mark) noexcept;

/// The start time of a process from the text of `/proc/<pid>/stat`: field 22, in clock ticks since the system started.
/// Field 2 is the program name in parentheses and may itself hold spaces and parentheses, so the fields are counted from
/// the last closing parenthesis. Empty when the line is not that of a stat file.
[[nodiscard]] std::optional<std::uint64_t> proc_stat_start_ticks(std::string_view stat) noexcept;

/// The text size multiplier for Android's `Configuration.fontScale`. 1.0 is the default size, as on iOS, and the range is
/// the iOS table's, 0.82 to 3.12, so that the interface is laid out for the same sizes on both phones. A scale that is not
/// a positive number is the default. Rounded to two decimals, so that a float's noise does not look like a change.
[[nodiscard]] double android_text_scale(double font_scale) noexcept;

/// Whether Reduce Motion is on: "Remove animations" (or Developer options) sets `Settings.Global.ANIMATOR_DURATION_SCALE`
/// to 0. Any other scale, however short, is motion.
[[nodiscard]] bool android_reduce_motion(double animator_scale) noexcept;

} // namespace ej::bridge
