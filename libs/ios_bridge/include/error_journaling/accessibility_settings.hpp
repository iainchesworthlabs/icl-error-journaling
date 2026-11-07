// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <string_view>

namespace ej::bridge
{

/// What the user has asked of the system that the interface must follow (NFR-07, Stage 4 decision 5).
struct AccessibilityState
{
    /// The text size multiplier from the content size category (Dynamic Type): 1.0 is the default "Large", about 0.82
    /// the smallest and about 3.1 the largest accessibility size.
    double m_TextScale = 1.0;
    /// Reduce Motion is on: springs and slides give way to a short fade.
    bool m_ReduceMotion = false;
    /// VoiceOver is running. For tests and diagnostics; the interface does not branch on it.
    bool m_VoiceOverRunning = false;

    friend bool operator==(const AccessibilityState&, const AccessibilityState&) = default;
};

/// The multiplier for a content size category, by the name UIKit gives it (UIContentSizeCategoryLarge is
/// "UICTContentSizeCategoryL", and so on). An unknown name, including an empty one, is 1.0: the default size.
///
/// The scales are Apple's body text sizes divided by the default 17 pt: xSmall 14, small 15, medium 16, large 17,
/// xLarge 19, xxLarge 21, xxxLarge 23, then accessibility 1 to 5 at 28, 33, 40, 47 and 53. So the table runs from 0.82
/// to 3.12 and rises with every step. Portable, so that the table is tested in CI and the Objective-C++ only passes a
/// string.
[[nodiscard]] double text_scale_for_category(std::string_view category_name) noexcept;

/// The system's accessibility settings and a notice when one changes. Every call is made on the main thread, and the
/// handler is called on it.
class AccessibilitySettings
{
public:
    AccessibilitySettings() = default;
    AccessibilitySettings(const AccessibilitySettings&) = delete;
    AccessibilitySettings& operator=(const AccessibilitySettings&) = delete;
    virtual ~AccessibilitySettings() = default;

    /// The settings now.
    [[nodiscard]] virtual AccessibilityState read() const = 0;

    /// `changed` is called after any of the three settings may have changed (it can be called when none did; the caller
    /// compares). Replaces an earlier handler; an empty one removes it. Nothing is called after the object is gone.
    virtual void set_change_handler(std::function<void()> changed) = 0;
};

/// ios/accessibility_settings.mm or desktop/accessibility_settings.cpp (1.0, false, false, and never changes).
[[nodiscard]] std::unique_ptr<AccessibilitySettings> make_accessibility_settings();

} // namespace ej::bridge
