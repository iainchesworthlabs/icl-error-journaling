// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace ej
{

/// "2026-11-07": the stored form of a log's local date. Four digit year, so years 0 to 9999 only.
[[nodiscard]] std::optional<std::string> format_iso_date(std::chrono::year_month_day date);

/// The date written by `format_iso_date`; nothing for any other text or for a date that does not exist.
[[nodiscard]] std::optional<std::chrono::year_month_day> parse_iso_date(std::string_view text);

}  // namespace ej
