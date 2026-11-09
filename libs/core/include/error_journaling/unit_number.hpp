// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "error_journaling/result.hpp"

namespace ej
{

/// A unit number (HDR-02): one to nine ASCII digits, no leading zero. `0` is not a unit number. It may carry an
/// environment (PRF-11): lower-case letters, digits and dashes, not starting or ending with a dash, at most
/// `MAX_ENVIRONMENT_LENGTH` characters, as in "UNIT-27-prod". The environment is optional; "UNIT-27" is a unit.
///
/// The stored text of a unit is the digits, then a dash and the environment when there is one: "27" or "27-prod".
/// Digits never contain a dash, so the first dash ends the digits. Text stored before environments existed is all
/// digits and reads as a unit without an environment.
class UnitNumber
{
public:
    enum class Error
    {
        EMPTY,
        NOT_DIGITS,
        LEADING_ZERO,
        TOO_LONG,
        BAD_ENVIRONMENT // not lower-case letters, digits and dashes, or empty, too long, or a dash at either end
    };

    static constexpr std::size_t MAX_DIGITS = 9;
    static constexpr std::size_t MAX_ENVIRONMENT_LENGTH = 20;

    /// The digits alone: no environment, and a dash is not a digit.
    [[nodiscard]] static Result<UnitNumber, Error> parse(std::string_view digits);

    /// The digits and an environment; an empty `environment` means none.
    [[nodiscard]] static Result<UnitNumber, Error> parse(std::string_view digits, std::string_view environment);

    /// The stored text: "27" or "27-prod".
    [[nodiscard]] static Result<UnitNumber, Error> parse_stored(std::string_view text);

    /// Whether `environment` is a usable environment name (not empty). Used for the choices of the setup file too.
    [[nodiscard]] static bool is_valid_environment(std::string_view environment);

    [[nodiscard]] std::string_view digits() const { return m_Digits; }
    /// Empty when the unit has no environment.
    [[nodiscard]] std::string_view environment() const
    {
        return m_Environment;
    }
    /// "27" or "27-prod": what is stored, and what `parse_stored` reads back.
    [[nodiscard]] std::string stored() const;
    /// "UNIT-27" or "UNIT-27-prod" for the prefix "UNIT"; the prefix is used verbatim.
    [[nodiscard]] std::string format(std::string_view prefix) const;

    friend bool operator==(const UnitNumber&, const UnitNumber&) = default;

private:
    UnitNumber(std::string digits, std::string environment)
        : m_Digits(std::move(digits)), m_Environment(std::move(environment))
    {
    }

    std::string m_Digits;
    std::string m_Environment;
};

/// The header label for the unit number: "UNIT #" for the prefix "UNIT".
[[nodiscard]] std::string unit_label(std::string_view prefix);

}  // namespace ej
