// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

#include "error_journaling/result.hpp"

namespace ej::config_seam
{

// SEAM (work package 1.7): Configuration needs `TimeFormat` (1.3) and `NoteTemplate` plus `validate_pattern` (1.2). They
// had not merged when 1.6 was written, so this header holds stand-ins with the same member names. In 1.7, delete this
// header, include `time_format.hpp` and `note_template.hpp`, and point the two aliases in `configuration.hpp` and the
// pattern check in `configuration.cpp` at the real ones. Nothing else in 1.6 depends on the stand-ins.

enum class TimeStyle
{
    H24,
    H12
};

enum class MeridiemCase
{
    UPPER,
    LOWER
};

struct TimeFormat
{
    TimeStyle m_Style = TimeStyle::H24;
    bool m_Seconds = false;
    MeridiemCase m_Meridiem = MeridiemCase::UPPER;

    friend bool operator==(const TimeFormat&, const TimeFormat&) = default;
};

enum class PatternError
{
    MISSING_PLACEHOLDER,
    UNBALANCED_BRACE,
    UNKNOWN_PLACEHOLDER
};

/// Only {value} is a placeholder; {{ and }} are literal braces; at least one {value} is required.
[[nodiscard]] Result<void, PatternError> validate_pattern(std::string_view pattern);

}  // namespace ej::config_seam
