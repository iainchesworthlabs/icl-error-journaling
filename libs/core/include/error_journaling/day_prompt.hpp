// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

#include "error_journaling/result.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "error_journaling/timestamp.hpp"

namespace ej
{

enum class DayPrompt
{
    NONE,
    OFFER_NEW_LOG
};

/// The calendar date of `timestamp` in its own zone.
[[nodiscard]] Result<std::chrono::year_month_day, TimeZoneProvider::Error> local_date(
    const Timestamp& timestamp, const TimeZoneProvider& zones);

/// Offered when the device-local date of `now` is later than the open log's date (DAY-01, DAY-02). No grace period.
[[nodiscard]] Result<DayPrompt, TimeZoneProvider::Error> day_prompt(
    std::chrono::year_month_day open_log_date, const Timestamp& now, const TimeZoneProvider& zones);

}  // namespace ej
