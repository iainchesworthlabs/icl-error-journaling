// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/extra_versions.hpp"

#include <algorithm>
#include <cctype>

namespace ej
{

namespace
{

constexpr char SEPARATOR = '=';
constexpr char LINE_END = '\n';

bool label_char(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == ' ' || c == '-';
}

std::string lower(std::string_view text)
{
    std::string out(text);
    std::ranges::transform(out, out.begin(), [](char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
    return out;
}

}  // namespace

Result<std::string, ExtraVersionsError> normalize_label(std::string_view label)
{
    std::string out;
    bool pending_space = false;
    for (const char c : label)
    {
        if (!label_char(c))
        {
            return fail(ExtraVersionsError::LABEL_CHARACTERS);
        }
        if (c == ' ')
        {
            pending_space = !out.empty();
            continue;
        }
        if (pending_space)
        {
            out.push_back(' ');
            pending_space = false;
        }
        out.push_back(c);
    }
    if (out.empty())
    {
        return fail(ExtraVersionsError::EMPTY_LABEL);
    }
    if (out.size() > MAX_EXTRA_LABEL_LENGTH)
    {
        return fail(ExtraVersionsError::LABEL_TOO_LONG);
    }
    if (lower(out) == lower(ROBOT_LABEL))
    {
        return fail(ExtraVersionsError::LABEL_IS_ROBOT);
    }
    return out;
}

Result<void, ExtraVersionsError> check_extra_versions(const ExtraVersions& versions)
{
    std::vector<std::string> seen;
    for (const auto& version : versions)
    {
        const auto label = normalize_label(version.label());
        if (!label)
        {
            return fail(label.error());
        }
        if (*label != version.label())
        {
            return fail(ExtraVersionsError::LABEL_CHARACTERS);
        }
        auto key = lower(*label);
        if (std::ranges::find(seen, key) != seen.end())
        {
            return fail(ExtraVersionsError::DUPLICATE_LABEL);
        }
        seen.push_back(std::move(key));
    }
    return {};
}

std::string stored_extra_versions(const ExtraVersions& versions)
{
    if (versions.empty())
    {
        return std::string(NO_EXTRA_VERSIONS);
    }
    std::string out;
    for (const auto& version : versions)
    {
        if (!out.empty())
        {
            out.push_back(LINE_END);
        }
        out += version.label();
        out.push_back(SEPARATOR);
        out += version.m_Version.stored();
    }
    return out;
}

Result<ExtraVersions, ExtraVersionsError> parse_extra_versions(std::string_view stored)
{
    ExtraVersions out;
    if (stored == NO_EXTRA_VERSIONS)
    {
        return out;
    }
    while (!stored.empty())
    {
        const auto end = stored.find(LINE_END);
        const auto line = stored.substr(0, end);
        stored = end == std::string_view::npos ? std::string_view{} : stored.substr(end + 1);
        const auto separator = line.find(SEPARATOR);
        if (separator == std::string_view::npos)
        {
            return fail(ExtraVersionsError::MALFORMED);
        }
        auto version = SoftwareVersion::parse_stored(line.substr(separator + 1));
        if (!version)
        {
            return fail(ExtraVersionsError::INVALID_VERSION);
        }
        if (line.substr(0, separator).size() > MAX_EXTRA_LABEL_LENGTH)
        {
            return fail(ExtraVersionsError::LABEL_TOO_LONG);
        }
        out.push_back(LabelledVersion{line.substr(0, separator), *version});
    }
    if (out.empty())
    {
        return fail(ExtraVersionsError::MALFORMED);
    }
    if (auto checked = check_extra_versions(out); !checked)
    {
        return fail(checked.error());
    }
    return out;
}

std::string extra_version_line_label(const LabelledVersion& version)
{
    return std::string(version.label()) + " Version";
}

std::string summarize_extra_versions(const ExtraVersions& versions)
{
    std::string out;
    for (const auto& version : versions)
    {
        if (!out.empty())
        {
            out += " \xC2\xB7 ";
        }
        out += std::string(version.label()) + ' ' + version.m_Version.display();
    }
    return out;
}

}  // namespace ej
