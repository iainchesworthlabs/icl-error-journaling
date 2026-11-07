// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <map>
#include <string>
#include <string_view>

#include "error_journaling/time_zone_provider.hpp"

namespace ej::test
{

/// A table of fixed UTC offsets per zone name: no daylight saving, no tz database. Other tests use it to stay
/// deterministic on every platform.
class FakeTimeZoneProvider final : public TimeZoneProvider
{
public:
    explicit FakeTimeZoneProvider(std::map<std::string, std::chrono::minutes, std::less<>> offsets)
        : m_Offsets(std::move(offsets))
    {
    }

    [[nodiscard]] Result<LocalTime, Error> to_local(const Timestamp& timestamp) const override
    {
        const auto it = m_Offsets.find(timestamp.m_Zone);
        if (it == m_Offsets.end())
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const auto local = std::chrono::floor<std::chrono::seconds>(timestamp.m_Utc + it->second);
        const auto days = std::chrono::floor<std::chrono::days>(local);
        return LocalTime{std::chrono::year_month_day{days}, local - days};
    }

    [[nodiscard]] Result<UtcMillis, Error> to_utc(const LocalTime& local, std::string_view zone) const override
    {
        const auto it = m_Offsets.find(zone);
        if (it == m_Offsets.end())
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const std::chrono::sys_days days{local.m_Date};
        return UtcMillis{std::chrono::floor<std::chrono::milliseconds>(days + local.m_SinceMidnight - it->second)};
    }

private:
    std::map<std::string, std::chrono::minutes, std::less<>> m_Offsets;
};

inline UtcMillis utc(int y, unsigned m, unsigned d, int hh, int mm, int ss = 0, int ms = 0)
{
    using namespace std::chrono;
    return UtcMillis{duration_cast<milliseconds>(sys_days{year{y} / month{m} / day{d}}.time_since_epoch()) +
                     hours{hh} + minutes{mm} + seconds{ss} + milliseconds{ms}};
}

inline LocalTime local(int y, unsigned m, unsigned d, int hh, int mm, int ss = 0)
{
    using namespace std::chrono;
    return LocalTime{year{y} / month{m} / day{d}, hours{hh} + minutes{mm} + seconds{ss}};
}

}  // namespace ej::test
