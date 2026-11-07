// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <string_view>

namespace icl_template
{

struct SemanticVersion
{
    int major = 0;
    int minor = 0;
    int patch = 0;

    friend constexpr bool operator==(const SemanticVersion&, const SemanticVersion&) = default;
};

enum class ParseError
{
    EMPTY,
    NOT_A_NUMBER,
    WRONG_FIELD_COUNT,
    OUT_OF_RANGE,
};

/// Parses "MAJOR.MINOR.PATCH". Each field is a decimal number of at most nine digits.
[[nodiscard]] std::expected<SemanticVersion, ParseError> parse_version(std::string_view text) noexcept;

}  // namespace icl_template
