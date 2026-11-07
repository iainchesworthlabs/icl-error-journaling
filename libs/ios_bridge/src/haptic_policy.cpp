// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/haptic_policy.hpp"

#include <utility>

namespace ej::bridge
{

HapticPolicy::HapticPolicy(Haptics& haptics, Clock clock, HapticPolicyConfig config)
    : m_Haptics(haptics), m_Clock(std::move(clock)), m_Config(config)
{
}

void HapticPolicy::set_board_visible(bool visible)
{
    const bool became_visible = visible && !m_BoardVisible;
    m_BoardVisible = visible;
    if (became_visible && ready())
    {
        prepare();
    }
}

void HapticPolicy::set_application_active(bool active)
{
    const bool became_active = active && !m_ApplicationActive;
    m_ApplicationActive = active;
    if (became_active && ready())
    {
        prepare();
    }
}

void HapticPolicy::tick()
{
    if (!m_Config.m_KeepAlive || !ready())
    {
        return;
    }
    if (!m_Prepared || m_Clock() - m_LastPrepare >= m_Config.m_Interval)
    {
        prepare();
    }
}

void HapticPolicy::prepare()
{
    m_Haptics.prepare();
    m_LastPrepare = m_Clock();
    m_Prepared = true;
}

} // namespace ej::bridge
