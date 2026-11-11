// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/android_haptics_plan.hpp"

namespace ej::bridge
{

int android_haptic_constant(HapticKind kind, int api_level) noexcept
{
    switch (kind)
    {
    case HapticKind::IMPACT:
        break;
    case HapticKind::SUCCESS:
        if (api_level >= android_haptic_confirm_since_api)
        {
            return android_haptic_confirm;
        }
        break;
    }
    return android_haptic_virtual_key;
}

} // namespace ej::bridge
