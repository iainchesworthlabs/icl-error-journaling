// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace error_journaling::bridge
{

/// Which implementation of the bridge is linked: "ios" on a phone, "desktop-stub" everywhere else. One implementation
/// per directory under src/platform/; CMake adds the one that matches the target and no source file tests for the
/// platform.
[[nodiscard]] std::string_view implementation_name() noexcept;

/// How a share sheet ended.
enum class share_outcome
{
    shared,       ///< The user chose an activity and it completed.
    cancelled,    ///< The user dismissed the sheet, or chose an activity and backed out of it.
    unavailable,  ///< This platform cannot present a share sheet (the desktop stub).
    failed,       ///< The sheet could not be presented, or the activity reported an error.
};

struct share_result
{
    share_outcome outcome{share_outcome::unavailable};
    /// The activity the user chose (for example "com.apple.UIKit.activity.CopyToPasteboard"), empty when none was.
    std::string activity_type;
};

/// The haptics the application fires.
enum class haptic_kind
{
    impact,   ///< A touch landed on a tile.
    success,  ///< A note was saved.
};

/// The fixed fictional text the Stage 0 share spike sends.
inline constexpr std::string_view spike_share_text = "Sam Example logged a Wrong Response on UNIT at North Site.";

using share_callback = std::function<void(const share_result&)>;

/// What Qt does not cover on an iPhone. Every call is made on the thread that runs the Qt event loop (the main thread
/// on iOS), and the share callback is delivered on that thread too.
class platform_services
{
public:
    platform_services() = default;
    platform_services(const platform_services&) = delete;
    platform_services& operator=(const platform_services&) = delete;
    virtual ~platform_services() = default;

    /// Presents the system share sheet with `text`. `done` is called exactly once. The iOS implementation calls it
    /// after this function has returned; the desktop stub calls it before.
    virtual void present_share_sheet(std::string_view text, share_callback done) = 0;

    /// Wakes the haptic engine so that the next fire_haptic has no start-up delay. Call it ahead of the touch (when the
    /// tile appears); fire_haptic prepares again by itself.
    virtual void prepare_haptics() = 0;

    /// Fires a haptic now. A no-op where there is no haptic engine.
    virtual void fire_haptic(haptic_kind kind) = 0;
};

/// The implementation for the platform CMake linked.
[[nodiscard]] std::unique_ptr<platform_services> make_platform_services();

}  // namespace error_journaling::bridge
