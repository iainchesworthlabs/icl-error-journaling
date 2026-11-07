// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptics.hpp"

#include <chrono>
#include <functional>

namespace ej::bridge
{

/// Whether the repeating keep-alive prepare is on. The Taptic Engine returns to idle a few seconds after a prepare
/// (Apple gives no figure). It is off until the device pass (Stage 5, M2, condition "after 60 seconds idle") shows it
/// is needed; switching it on is this one constant, then a rebuild.
inline constexpr bool HAPTIC_KEEPALIVE = false;
inline constexpr std::chrono::milliseconds HAPTIC_KEEPALIVE_INTERVAL{4000};

struct HapticPolicyConfig
{
    bool m_KeepAlive = HAPTIC_KEEPALIVE;
    std::chrono::milliseconds m_Interval = HAPTIC_KEEPALIVE_INTERVAL;
};

/// When to prepare the haptic generators, so that a fire on touch-down has no start-up delay (BRD-03, NFR-03). The
/// generator must be prepared ahead of the touch and fired on it; this class decides "ahead".
///
/// It prepares when the board becomes visible, when the application becomes active, and, when the keep-alive is on, on
/// each tick once the interval has passed since the last prepare. It prepares on none of them while the application is
/// inactive or the board is hidden. Firing prepares again by itself (in the Haptics implementation), so the policy does
/// not. The clock is injected so that a test drives the keep-alive without sleeping. Main thread only.
class HapticPolicy
{
public:
    using Clock = std::function<std::chrono::milliseconds()>;

    /// `haptics` is not owned and must outlive the policy. A board that is not yet shown is hidden; the
    /// application is taken to be active, as it is at launch.
    HapticPolicy(Haptics& haptics, Clock clock, HapticPolicyConfig config = {});

    void set_board_visible(bool visible);
    void set_application_active(bool active);

    /// The keep-alive timer fired. Does nothing when the keep-alive is off, the board is hidden or the application is
    /// inactive, or the interval has not passed since the last prepare.
    void tick();

    [[nodiscard]] bool keep_alive_enabled() const noexcept
    {
        return m_Config.m_KeepAlive;
    }
    [[nodiscard]] std::chrono::milliseconds interval() const noexcept
    {
        return m_Config.m_Interval;
    }

private:
    void prepare();
    [[nodiscard]] bool ready() const noexcept
    {
        return m_BoardVisible && m_ApplicationActive;
    }

    Haptics& m_Haptics;
    Clock m_Clock;
    HapticPolicyConfig m_Config;
    bool m_BoardVisible = false;
    bool m_ApplicationActive = true;
    std::chrono::milliseconds m_LastPrepare{0};
    bool m_Prepared = false;
};

} // namespace ej::bridge
