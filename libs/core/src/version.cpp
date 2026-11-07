// SPDX-License-Identifier: GPL-3.0-or-later
#include <charconv>
#include <string_view>

#include "icl_template/parse.hpp"

namespace icl_template
{

namespace
{

std::expected<int, ParseError> parse_field(std::string_view field) noexcept
{
    if (field.empty())
    {
        return std::unexpected(ParseError::NOT_A_NUMBER);
    }
    // from_chars accepts a leading '-', so check for digits first.
    for (const char c : field)
    {
        if (c < '0' || c > '9')
        {
            return std::unexpected(ParseError::NOT_A_NUMBER);
        }
    }
    if (field.size() > 9)
    {
        return std::unexpected(ParseError::OUT_OF_RANGE);
    }
    int value = 0;
    const auto [end, ec] = std::from_chars(field.data(), field.data() + field.size(), value);
    if (ec != std::errc{} || end != field.data() + field.size())
    {
        return std::unexpected(ParseError::NOT_A_NUMBER);
    }
    return value;
}

}  // namespace

std::expected<SemanticVersion, ParseError> parse_version(std::string_view text) noexcept
{
    if (text.empty())
    {
        return std::unexpected(ParseError::EMPTY);
    }

    int fields[3] = {0, 0, 0};
    int count = 0;
    while (true)
    {
        const auto dot = text.find('.');
        const auto piece = text.substr(0, dot);
        if (count == 3)
        {
            return std::unexpected(ParseError::WRONG_FIELD_COUNT);
        }
        const auto parsed = parse_field(piece);
        if (!parsed)
        {
            return std::unexpected(parsed.error());
        }
        fields[count++] = *parsed;
        if (dot == std::string_view::npos)
        {
            break;
        }
        text.remove_prefix(dot + 1);
    }

    if (count != 3)
    {
        return std::unexpected(ParseError::WRONG_FIELD_COUNT);
    }
    return SemanticVersion{fields[0], fields[1], fields[2]};
}

}  // namespace icl_template
