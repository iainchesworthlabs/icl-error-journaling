// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig/manual_animation_driver.hpp"

namespace ej::ui
{

ManualAnimationDriver::ManualAnimationDriver()
{
    install();
}

ManualAnimationDriver::~ManualAnimationDriver()
{
    uninstall();
}

void ManualAnimationDriver::step(qint64 milliseconds)
{
    m_Elapsed += milliseconds;
    advanceAnimation();
}

} // namespace ej::ui
