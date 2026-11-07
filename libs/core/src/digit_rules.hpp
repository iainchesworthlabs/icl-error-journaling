// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string_view>

namespace ej::detail
{

enum class DigitCheck
{
    OK,
    EMPTY,
    NOT_DIGITS,
    LEADING_ZERO,
    TOO_LONG
};

/// Shared by UnitNumber and SoftwareVersion. ASCII digits only, at most `max_digits`, and no leading zero. A lone "0" is
/// a leading zero unless `allow_lone_zero`. The checks run in the order EMPTY, NOT_DIGITS, LEADING_ZERO, TOO_LONG.
[[nodiscard]] constexpr DigitCheck check_digits(std::string_view text, std::size_t max_digits, bool allow_lone_zero)
{
    if (text.empty())
    {
        return DigitCheck::EMPTY;
    }
    for (const char c : text)
    {
        if (c < '0' || c > '9')
        {
            return DigitCheck::NOT_DIGITS;
        }
    }
    if (text.front() == '0' && !(allow_lone_zero && text.size() == 1))
    {
        return DigitCheck::LEADING_ZERO;
    }
    if (text.size() > max_digits)
    {
        return DigitCheck::TOO_LONG;
    }
    return DigitCheck::OK;
}

}  // namespace ej::detail
