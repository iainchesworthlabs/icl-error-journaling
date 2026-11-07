// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/unit_number.hpp"

#include "digit_rules.hpp"

namespace ej
{

Result<UnitNumber, UnitNumber::Error> UnitNumber::parse(std::string_view digits)
{
    switch (detail::check_digits(digits, MAX_DIGITS, false))
    {
    case detail::DigitCheck::OK: return UnitNumber(std::string(digits));
    case detail::DigitCheck::EMPTY: return fail(Error::EMPTY);
    case detail::DigitCheck::NOT_DIGITS: return fail(Error::NOT_DIGITS);
    case detail::DigitCheck::LEADING_ZERO: return fail(Error::LEADING_ZERO);
    case detail::DigitCheck::TOO_LONG: return fail(Error::TOO_LONG);
    }
    return fail(Error::NOT_DIGITS);  // GCOVR_EXCL_LINE unreachable: every DigitCheck value is handled above
}

std::string UnitNumber::format(std::string_view prefix) const
{
    std::string out(prefix);
    out += '-';
    out += m_Digits;
    return out;
}

std::string unit_label(std::string_view prefix)
{
    std::string out(prefix);
    out += " #";
    return out;
}

}  // namespace ej
