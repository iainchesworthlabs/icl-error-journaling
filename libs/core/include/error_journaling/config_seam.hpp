// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace ej::config_seam
{

// SEAM (work package 1.7): Configuration needs `TimeFormat` (1.3), which had not merged when 1.6 was written, so this
// header holds a stand-in with the same member names. In 1.7, delete this header,
// include `time_format.hpp`, and point the `ConfigTimeFormat` alias in `configuration.hpp`, the `config_seam::` uses in
// `configuration.cpp` and the tests at the real `TimeFormat`, `TimeStyle` and `MeridiemCase`.

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

}  // namespace ej::config_seam
