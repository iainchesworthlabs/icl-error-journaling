// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/android_import_plan.hpp"

namespace ej::bridge
{

PickOutcome android_pick_outcome_from_code(int code) noexcept
{
    switch (code)
    {
    case 0:
        return PickOutcome::PICKED;
    case 1:
        return PickOutcome::CANCELLED;
    default:
        return PickOutcome::FAILED;
    }
}

} // namespace ej::bridge
