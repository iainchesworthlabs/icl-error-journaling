// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <string>

namespace ej
{

using UtcMillis = std::chrono::sys_time<std::chrono::milliseconds>;

/// When something was captured: the UTC instant and the IANA zone in effect (for example "Australia/Melbourne").
struct Timestamp
{
    UtcMillis m_Utc;
    std::string m_Zone;
};

/// A wall-clock time in some zone: the local date and the time since local midnight.
struct LocalTime
{
    std::chrono::year_month_day m_Date;
    std::chrono::seconds m_SinceMidnight;
};

}  // namespace ej
