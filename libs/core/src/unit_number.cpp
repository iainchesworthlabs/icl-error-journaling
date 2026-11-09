// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/unit_number.hpp"

#include "digit_rules.hpp"

#include <algorithm>

namespace ej
{

Result<UnitNumber, UnitNumber::Error> UnitNumber::parse(std::string_view digits)
{
    return parse(digits, {});
}

bool UnitNumber::is_valid_environment(std::string_view environment)
{
    if (environment.empty() || environment.size() > MAX_ENVIRONMENT_LENGTH || environment.front() == '-' ||
        environment.back() == '-')
    {
        return false;
    }
    return std::ranges::all_of(environment,
                               [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-'; });
}

Result<UnitNumber, UnitNumber::Error> UnitNumber::parse_stored(std::string_view text)
{
    const auto dash = text.find('-');
    if (dash == std::string_view::npos)
    {
        return parse(text, {});
    }
    // The digits are judged first, so "x-prod" says what is wrong with the number; "27-" has a dash and no environment.
    const auto digits = text.substr(0, dash);
    if (auto parsed = parse(digits, {}); !parsed)
    {
        return parsed;
    }
    const auto environment = text.substr(dash + 1);
    if (environment.empty())
    {
        return fail(Error::BAD_ENVIRONMENT);
    }
    return parse(digits, environment);
}

Result<UnitNumber, UnitNumber::Error> UnitNumber::parse(std::string_view digits, std::string_view environment)
{
    switch (detail::check_digits(digits, MAX_DIGITS, false))
    {
    case detail::DigitCheck::OK:
        if (!environment.empty() && !is_valid_environment(environment))
        {
            return fail(Error::BAD_ENVIRONMENT);
        }
        return UnitNumber(std::string(digits), std::string(environment));
    case detail::DigitCheck::EMPTY: return fail(Error::EMPTY);
    case detail::DigitCheck::NOT_DIGITS: return fail(Error::NOT_DIGITS);
    case detail::DigitCheck::LEADING_ZERO: return fail(Error::LEADING_ZERO);
    case detail::DigitCheck::TOO_LONG: return fail(Error::TOO_LONG);
    }
    return fail(Error::NOT_DIGITS);  // GCOVR_EXCL_LINE unreachable: every DigitCheck value is handled above
}

std::string UnitNumber::stored() const
{
    std::string out = m_Digits;
    if (!m_Environment.empty())
    {
        out += '-';
        out += m_Environment;
    }
    return out;
}

std::string UnitNumber::format(std::string_view prefix) const
{
    std::string out(prefix);
    out += '-';
    out += stored();
    return out;
}

std::string unit_label(std::string_view prefix)
{
    std::string out(prefix);
    out += " #";
    return out;
}

}  // namespace ej
