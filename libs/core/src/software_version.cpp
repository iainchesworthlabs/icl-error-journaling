// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/software_version.hpp"

#include "digit_rules.hpp"

#include <algorithm>
#include <array>
#include <iterator>

namespace ej
{

namespace
{

constexpr std::size_t MAX_COMPONENT_DIGITS = 9;
constexpr char COMMIT_SEPARATOR = '+';

bool is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

char lower_hex(char c)
{
    return (c >= 'A' && c <= 'F') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

Result<std::string, SoftwareVersion::Error> SoftwareVersion::normalize_commit(std::string_view commit)
{
    if (commit.empty())
    {
        return std::string{};
    }
    if (!std::ranges::all_of(commit, is_hex))
    {
        return fail(Error::COMMIT_NOT_HEX);
    }
    std::string out;
    out.reserve(commit.size());
    std::ranges::transform(commit, std::back_inserter(out), lower_hex);
    if (out.size() == FULL_COMMIT_LENGTH)
    {
        out.resize(MIN_COMMIT_LENGTH);
    }
    else if (out.size() < MIN_COMMIT_LENGTH || out.size() > MAX_COMMIT_LENGTH)
    {
        return fail(Error::COMMIT_LENGTH);
    }
    return out;
}

Result<SoftwareVersion, SoftwareVersion::Error> SoftwareVersion::parse_stored(std::string_view text)
{
    const auto plus = text.find(COMMIT_SEPARATOR);
    if (plus == std::string_view::npos)
    {
        return parse(text, {});
    }
    // The components are judged first, so "x+a1b2c3d" says what is wrong with the version; "1.4.2+" has a separator and
    // no commit.
    const auto components = text.substr(0, plus);
    if (auto parsed = parse(components, {}); !parsed)
    {
        return parsed;
    }
    const auto commit = text.substr(plus + 1);
    if (commit.empty())
    {
        return fail(Error::COMMIT_LENGTH);
    }
    return parse(components, commit);
}

Result<SoftwareVersion, SoftwareVersion::Error> SoftwareVersion::parse(std::string_view text)
{
    return parse(text, {});
}

Result<SoftwareVersion, SoftwareVersion::Error> SoftwareVersion::parse(std::string_view text, std::string_view commit)
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
    auto kept = normalize_commit(commit);
    if (!kept)
    {
        return fail(kept.error());
    }
    return SoftwareVersion(values[0], values[1], values[2], std::move(*kept));
}

std::string SoftwareVersion::format() const
{
    return std::to_string(m_Major) + '.' + std::to_string(m_Minor) + '.' + std::to_string(m_Patch);
}

std::string SoftwareVersion::stored() const
{
    std::string out = format();
    if (!m_Commit.empty())
    {
        out += COMMIT_SEPARATOR;
        out += m_Commit;
    }
    return out;
}

std::string SoftwareVersion::display() const
{
    std::string out = format();
    if (!m_Commit.empty())
    {
        out += " (";
        out += m_Commit;
        out += ')';
    }
    return out;
}

}  // namespace ej
