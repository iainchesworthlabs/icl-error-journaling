// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"

namespace ej
{

struct NoteTemplate
{
    NoteTemplateId m_Id;
    std::string m_Name;           // "Played song"
    std::string m_ValuePrompt;    // "Which song?"
    std::string m_OutputPattern;  // "Played {value}"
    int m_SortOrder = 0;
};

enum class PatternError
{
    MISSING_PLACEHOLDER,
    UNBALANCED_BRACE,
    UNKNOWN_PLACEHOLDER
};

/// Only {value} is a placeholder; a literal brace is written {{ or }}. The pattern must contain {value} at least once.
[[nodiscard]] Result<void, PatternError> validate_pattern(std::string_view pattern);

/// Substitutes the value, trimmed of surrounding ASCII whitespace, for each {value}. The value is inserted verbatim
/// and never expanded again. Total: a pattern that does not validate is rendered with its stray braces as they are.
[[nodiscard]] std::string render_note(const NoteTemplate& note_template, std::string_view value);

}  // namespace ej
