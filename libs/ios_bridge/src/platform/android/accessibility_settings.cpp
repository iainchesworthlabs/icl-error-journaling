// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 9: until package 9.6 has an Android implementation of this capability it is the desktop stub (a copy of
// ../desktop/accessibility_settings.cpp). check_platform_layout.py wants one file per capability in every platform directory.
#include "error_journaling/accessibility_settings.hpp"

namespace ej::bridge
{

namespace
{

// The default size, no Reduce Motion, no VoiceOver, and nothing ever changes. Tests that need other values hand the
// bridge their own AccessibilitySettings.
class DesktopAccessibilitySettings final : public AccessibilitySettings
{
public:
    [[nodiscard]] AccessibilityState read() const override
    {
        return {};
    }

    void set_change_handler(std::function<void()> /*changed*/) override {}
};

} // namespace

std::unique_ptr<AccessibilitySettings> make_accessibility_settings()
{
    return std::make_unique<DesktopAccessibilitySettings>();
}

} // namespace ej::bridge
