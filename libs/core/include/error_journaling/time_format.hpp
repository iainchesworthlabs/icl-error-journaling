// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

#include "error_journaling/timestamp.hpp"

namespace ej
{

enum class TimeStyle
{
    H24,
    H12
};

/// `AM`/`PM` or `am`/`pm`; only used with `TimeStyle::H12`.
enum class MeridiemCase
{
    UPPER,
    LOWER
};

/// The clock format preference (PRF-07). The defaults are 24-hour `HH:mm`.
struct TimeFormat
{
    TimeStyle m_Style = TimeStyle::H24;
    bool m_Seconds = false;
    MeridiemCase m_Meridiem = MeridiemCase::UPPER;

    friend bool operator==(const TimeFormat&, const TimeFormat&) = default;
};

/// "10:05", "10:05:09", "10:05 AM", "10:05:09 am". Locale-independent; the hour keeps its leading zero.
[[nodiscard]] std::string format_time(const LocalTime& time, TimeFormat format);

}  // namespace ej
