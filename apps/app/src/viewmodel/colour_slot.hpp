// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "error_journaling/ids.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>

namespace ej::viewmodel
{

/// The number of tile colours in the design tokens (`docs/design/tokens.json`, slots 1 to 11). Slot 12 is Other's.
inline constexpr int COLOUR_SLOT_COUNT = 11;

/// What a model reports as the colour slot of the Other tile and of an empty tile: no type colour.
inline constexpr int NO_COLOUR_SLOT = 0;

/// The colour slot of the error type at `master_index` in the master list (`Configuration::m_ErrorTypes`, retired types
/// included, in order): the index modulo 11, plus one, so slots 1 to 11. The twelfth type reuses slot 1. The colour
/// belongs to the type and not to its board position, so moving a tile never changes it (Stage 3, decision 8). No
/// colour is stored in the configuration.
[[nodiscard]] constexpr int colour_slot_for_index(std::size_t master_index) noexcept
{
    return static_cast<int>(master_index % static_cast<std::size_t>(COLOUR_SLOT_COUNT)) + 1;
}

/// The colour slot of `type`, or nothing for an id that is not in the master list.
[[nodiscard]] inline std::optional<int> colour_slot_for(const Configuration& configuration, ErrorTypeId type)
{
    const auto found = std::ranges::find(configuration.m_ErrorTypes, type, &ErrorType::m_Id);
    if (found == configuration.m_ErrorTypes.end())
    {
        return std::nullopt;
    }
    return colour_slot_for_index(static_cast<std::size_t>(found - configuration.m_ErrorTypes.begin()));
}

} // namespace ej::viewmodel
