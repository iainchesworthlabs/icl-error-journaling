// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/note_template.hpp"

#include <optional>

namespace ej
{

namespace
{

constexpr std::string_view PLACEHOLDER = "{value}";

bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && is_space(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

struct Expansion
{
    std::string m_Text;
    std::size_t m_Placeholders = 0;
    std::optional<PatternError> m_Problem;  // the first structural problem; the scan carries on
};

/// One scanner for both validation and rendering, so they cannot disagree about what a pattern means.
Expansion expand(std::string_view pattern, std::string_view value)
{
    Expansion result;
    for (std::size_t i = 0; i < pattern.size();)
    {
        const char c = pattern[i];
        if (c == '{' && i + 1 < pattern.size() && pattern[i + 1] == '{')
        {
            result.m_Text += '{';
            i += 2;
        }
        else if (c == '}' && i + 1 < pattern.size() && pattern[i + 1] == '}')
        {
            result.m_Text += '}';
            i += 2;
        }
        else if (c == '{')
        {
            if (pattern.substr(i, PLACEHOLDER.size()) == PLACEHOLDER)
            {
                result.m_Text += value;
                ++result.m_Placeholders;
                i += PLACEHOLDER.size();
            }
            else
            {
                if (!result.m_Problem)
                {
                    result.m_Problem = pattern.find('}', i) == std::string_view::npos ? PatternError::UNBALANCED_BRACE
                                                                                      : PatternError::UNKNOWN_PLACEHOLDER;
                }
                result.m_Text += c;
                ++i;
            }
        }
        else
        {
            if (c == '}' && !result.m_Problem)
            {
                result.m_Problem = PatternError::UNBALANCED_BRACE;
            }
            result.m_Text += c;
            ++i;
        }
    }
    return result;
}

}  // namespace

Result<void, PatternError> validate_pattern(std::string_view pattern)
{
    const auto expansion = expand(pattern, {});
    if (expansion.m_Problem)
    {
        return fail(*expansion.m_Problem);
    }
    if (expansion.m_Placeholders == 0)
    {
        return fail(PatternError::MISSING_PLACEHOLDER);
    }
    return {};
}

std::string render_note(const NoteTemplate& note_template, std::string_view value)
{
    return expand(note_template.m_OutputPattern, trim(value)).m_Text;
}

}  // namespace ej
