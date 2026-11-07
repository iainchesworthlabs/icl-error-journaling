// SPDX-License-Identifier: GPL-3.0-or-later
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
