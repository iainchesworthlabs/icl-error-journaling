// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <compare>
#include <cstdint>
#include <string>
#include <string_view>

#include "error_journaling/result.hpp"

namespace ej
{

/// major.minor.patch (HDR-04): three components of one to nine ASCII digits, no leading zeros (a lone 0 is fine).
class SoftwareVersion
{
public:
    enum class Error
    {
        EMPTY,
        WRONG_COMPONENT_COUNT,
        NOT_DIGITS,
        LEADING_ZERO,
        TOO_LONG
    };

    [[nodiscard]] static Result<SoftwareVersion, Error> parse(std::string_view text);

    [[nodiscard]] std::string format() const;

    friend auto operator<=>(const SoftwareVersion&, const SoftwareVersion&) = default;

private:
    SoftwareVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch)
        : m_Major(major), m_Minor(minor), m_Patch(patch)
    {
    }

    std::uint32_t m_Major = 0;
    std::uint32_t m_Minor = 0;
    std::uint32_t m_Patch = 0;
};

}  // namespace ej
