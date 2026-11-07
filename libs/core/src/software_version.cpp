// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/software_version.hpp"

#include <array>

#include "digit_rules.hpp"

namespace ej
{

namespace
{

constexpr std::size_t MAX_COMPONENT_DIGITS = 9;

}  // namespace

Result<SoftwareVersion, SoftwareVersion::Error> SoftwareVersion::parse(std::string_view text)
{
    if (text.empty())
    {
        return fail(Error::EMPTY);
    }
    std::array<std::string_view, 3> parts;
    std::size_t count = 0;
    std::size_t start = 0;
    while (true)
    {
        const auto dot = text.find('.', start);
        const auto part = text.substr(start, dot == std::string_view::npos ? dot : dot - start);
        if (count == parts.size())
        {
            return fail(Error::WRONG_COMPONENT_COUNT);
        }
        parts[count++] = part;
        if (dot == std::string_view::npos)
        {
            break;
        }
        start = dot + 1;
    }
    if (count != parts.size())
    {
        return fail(Error::WRONG_COMPONENT_COUNT);
    }

    std::array<std::uint32_t, 3> values{};
    for (std::size_t i = 0; i < parts.size(); ++i)
    {
        switch (detail::check_digits(parts[i], MAX_COMPONENT_DIGITS, true))
        {
        case detail::DigitCheck::OK: break;
        case detail::DigitCheck::EMPTY:
        case detail::DigitCheck::NOT_DIGITS: return fail(Error::NOT_DIGITS);
        case detail::DigitCheck::LEADING_ZERO: return fail(Error::LEADING_ZERO);
        case detail::DigitCheck::TOO_LONG: return fail(Error::TOO_LONG);
        }
        std::uint32_t value = 0;
        for (const char c : parts[i])
        {
            value = value * 10 + static_cast<std::uint32_t>(c - '0');
        }
        values[i] = value;
    }
    return SoftwareVersion(values[0], values[1], values[2]);
}

std::string SoftwareVersion::format() const
{
    return std::to_string(m_Major) + '.' + std::to_string(m_Minor) + '.' + std::to_string(m_Patch);
}

}  // namespace ej
