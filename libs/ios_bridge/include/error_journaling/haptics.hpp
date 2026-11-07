// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

namespace ej::bridge
{

enum class HapticKind
{
    IMPACT,  ///< A touch landed on a tile.
    SUCCESS, ///< A note was saved.
};

/// The Taptic Engine. Every call is made on the main thread.
class Haptics
{
public:
    Haptics() = default;
    Haptics(const Haptics&) = delete;
    Haptics& operator=(const Haptics&) = delete;
    virtual ~Haptics() = default;

    /// Wakes the generators so that the next fire has no start-up delay. Call it ahead of the touch (when the board
    /// appears); fire prepares again by itself.
    virtual void prepare() = 0;

    /// Fires a haptic now, then prepares again. Does nothing where there is no haptic engine.
    virtual void fire(HapticKind kind) = 0;
};

/// ios/haptics.mm or desktop/haptics.cpp (does nothing).
[[nodiscard]] std::unique_ptr<Haptics> make_haptics();

} // namespace ej::bridge
