// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/haptics.hpp"

namespace ej::bridge
{

/// The `android.view.HapticFeedbackConstants` value for a HapticKind on a given Android API level, in portable C++ so
/// that a test covers the table on the desktop (docs/plan/stage-9.md, "Haptics"). The numbers are Android's own:
/// VIRTUAL_KEY is 1, and CONFIRM (16) exists from API 30.
///
/// - IMPACT, a touch landing on a tile: VIRTUAL_KEY.
/// - SUCCESS, a note saved: CONFIRM where it exists, else VIRTUAL_KEY.
inline constexpr int android_haptic_virtual_key = 1;
inline constexpr int android_haptic_confirm = 16;
inline constexpr int android_haptic_confirm_since_api = 30;

[[nodiscard]] int android_haptic_constant(HapticKind kind, int api_level) noexcept;

} // namespace ej::bridge
