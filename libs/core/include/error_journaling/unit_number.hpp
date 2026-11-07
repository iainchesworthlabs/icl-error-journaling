// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "error_journaling/result.hpp"

namespace ej
{

/// A unit number (HDR-02): one to nine ASCII digits, no leading zero. `0` is not a unit number.
class UnitNumber
{
public:
    enum class Error
    {
        EMPTY,
        NOT_DIGITS,
        LEADING_ZERO,
        TOO_LONG
    };

    static constexpr std::size_t MAX_DIGITS = 9;

    [[nodiscard]] static Result<UnitNumber, Error> parse(std::string_view digits);

    [[nodiscard]] std::string_view digits() const { return m_Digits; }
    /// "UNIT-27" for the prefix "UNIT"; the prefix is used verbatim.
    [[nodiscard]] std::string format(std::string_view prefix) const;

    friend bool operator==(const UnitNumber&, const UnitNumber&) = default;

private:
    explicit UnitNumber(std::string digits) : m_Digits(std::move(digits)) {}

    std::string m_Digits;
};

/// The header label for the unit number: "UNIT #" for the prefix "UNIT".
[[nodiscard]] std::string unit_label(std::string_view prefix);

}  // namespace ej
