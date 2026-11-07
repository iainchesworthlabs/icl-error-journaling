// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/time_format.hpp"

#include <cstdint>

namespace ej
{
namespace
{

void append_two_digits(std::string& out, std::int64_t value)
{
    out.push_back(static_cast<char>('0' + (value / 10) % 10));
    out.push_back(static_cast<char>('0' + value % 10));
}

}  // namespace

std::string format_time(const LocalTime& time, TimeFormat format)
{
    const std::int64_t total = time.m_SinceMidnight.count();
    const std::int64_t hour24 = (total / 3600) % 24;
    const std::int64_t minute = (total / 60) % 60;
    const std::int64_t second = total % 60;

    std::int64_t hour = hour24;
    if (format.m_Style == TimeStyle::H12)
    {
        hour = hour24 % 12 == 0 ? 12 : hour24 % 12;
    }

    std::string out;
    append_two_digits(out, hour);
    out.push_back(':');
    append_two_digits(out, minute);
    if (format.m_Seconds)
    {
        out.push_back(':');
        append_two_digits(out, second);
    }
    if (format.m_Style == TimeStyle::H12)
    {
        const bool pm = hour24 >= 12;
        if (format.m_Meridiem == MeridiemCase::UPPER)
        {
            out += pm ? " PM" : " AM";
        }
        else
        {
            out += pm ? " pm" : " am";
        }
    }
    return out;
}

}  // namespace ej
