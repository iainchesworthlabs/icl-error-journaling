// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/day_prompt.hpp"

namespace ej
{

Result<std::chrono::year_month_day, TimeZoneProvider::Error> local_date(
    const Timestamp& timestamp, const TimeZoneProvider& zones)
{
    auto local = zones.to_local(timestamp);
    if (!local)
    {
        return fail(local.error());
    }
    return local->m_Date;
}

Result<DayPrompt, TimeZoneProvider::Error> day_prompt(
    std::chrono::year_month_day open_log_date, const Timestamp& now, const TimeZoneProvider& zones)
{
    auto today = local_date(now, zones);
    if (!today)
    {
        return fail(today.error());
    }
    // Compared as dates, not elapsed time, so 23- and 25-hour days roll over at local midnight.
    const std::chrono::sys_days today_days{*today};
    const std::chrono::sys_days log_days{open_log_date};
    return today_days > log_days ? DayPrompt::OFFER_NEW_LOG : DayPrompt::NONE;
}

}  // namespace ej
