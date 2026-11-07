// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/iso_date.hpp"

#include <charconv>
#include <cstdio>

namespace ej
{

std::optional<std::string> format_iso_date(std::chrono::year_month_day date)
{
    const int year = static_cast<int>(date.year());
    if (!date.ok() || year < 0 || year > 9999)
    {
        return std::nullopt;
    }
    char buffer[16];
    const int written = std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02u", year,
                                      static_cast<unsigned>(date.month()), static_cast<unsigned>(date.day()));
    return std::string(buffer, static_cast<std::size_t>(written));
}

std::optional<std::chrono::year_month_day> parse_iso_date(std::string_view text)
{
    const auto number = [&text](std::size_t from, std::size_t count) -> int
    {
        int value = -1;
        const auto [end, code] = std::from_chars(text.data() + from, text.data() + from + count, value);
        return code == std::errc{} && end == text.data() + from + count ? value : -1;
    };
    if (text.size() != 10 || text[4] != '-' || text[7] != '-')
    {
        return std::nullopt;
    }
    const int year = number(0, 4);
    const int month = number(5, 2);
    const int day = number(8, 2);
    if (year < 0 || month < 0 || day < 0)
    {
        return std::nullopt;
    }
    const std::chrono::year_month_day date{std::chrono::year{year}, std::chrono::month{static_cast<unsigned>(month)},
                                           std::chrono::day{static_cast<unsigned>(day)}};
    if (!date.ok())
    {
        return std::nullopt;
    }
    return date;
}

}  // namespace ej
