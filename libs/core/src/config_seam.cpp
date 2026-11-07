// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/config_seam.hpp"

#include <string>

namespace ej::config_seam
{

Result<void, PatternError> validate_pattern(std::string_view pattern)
{
    bool found = false;
    for (std::size_t i = 0; i < pattern.size(); ++i)
    {
        const char c = pattern[i];
        if (c == '{')
        {
            if (i + 1 < pattern.size() && pattern[i + 1] == '{')
            {
                ++i;
                continue;
            }
            const auto close = pattern.find('}', i + 1);
            if (close == std::string_view::npos)
            {
                return fail(PatternError::UNBALANCED_BRACE);
            }
            const auto inner = pattern.substr(i + 1, close - i - 1);
            if (inner.find('{') != std::string_view::npos)
            {
                return fail(PatternError::UNBALANCED_BRACE);
            }
            if (inner != "value")
            {
                return fail(PatternError::UNKNOWN_PLACEHOLDER);
            }
            found = true;
            i = close;
        }
        else if (c == '}')
        {
            if (i + 1 < pattern.size() && pattern[i + 1] == '}')
            {
                ++i;
                continue;
            }
            return fail(PatternError::UNBALANCED_BRACE);
        }
    }
    if (!found)
    {
        return fail(PatternError::MISSING_PLACEHOLDER);
    }
    return {};
}

}  // namespace ej::config_seam
