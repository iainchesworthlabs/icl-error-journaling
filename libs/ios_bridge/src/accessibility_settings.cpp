// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/accessibility_settings.hpp"

#include <array>
#include <utility>

namespace ej::bridge
{

namespace
{

struct CategoryScale
{
    std::string_view m_Name;
    double m_Scale;
};

// UIContentSizeCategory raw values. The body point size of each, over 17.
constexpr std::array<CategoryScale, 12> CATEGORY_SCALES{{
    {"UICTContentSizeCategoryXS", 14.0 / 17.0},
    {"UICTContentSizeCategoryS", 15.0 / 17.0},
    {"UICTContentSizeCategoryM", 16.0 / 17.0},
    {"UICTContentSizeCategoryL", 1.0},
    {"UICTContentSizeCategoryXL", 19.0 / 17.0},
    {"UICTContentSizeCategoryXXL", 21.0 / 17.0},
    {"UICTContentSizeCategoryXXXL", 23.0 / 17.0},
    {"UICTContentSizeCategoryAccessibilityM", 28.0 / 17.0},
    {"UICTContentSizeCategoryAccessibilityL", 33.0 / 17.0},
    {"UICTContentSizeCategoryAccessibilityXL", 40.0 / 17.0},
    {"UICTContentSizeCategoryAccessibilityXXL", 47.0 / 17.0},
    {"UICTContentSizeCategoryAccessibilityXXXL", 53.0 / 17.0},
}};

} // namespace

double text_scale_for_category(std::string_view category_name) noexcept
{
    for (const CategoryScale& entry : CATEGORY_SCALES)
    {
        if (entry.m_Name == category_name)
        {
            return entry.m_Scale;
        }
    }
    return 1.0;
}

} // namespace ej::bridge
