// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The interaction types in force (Stage 14, package I-6): the types the user made, as the error types are. The app has
// no built-in interaction type: with none in the setup the Interactions board is empty and its tiles offer setup.

#include "error_journaling/configuration.hpp"
#include "error_journaling/interaction.hpp"

#include <array>
#include <cstddef>
#include <optional>
#include <vector>

namespace ej::viewmodel
{

[[nodiscard]] std::vector<InteractionType> interaction_types_in_force(const Configuration& configuration);

/// The master-list index of the live type on each board position, as `board_layout` does for the error types.
using InteractionLayout = std::array<std::optional<std::size_t>, CONFIG_BOARD_POSITIONS>;
[[nodiscard]] InteractionLayout interaction_layout(const std::vector<InteractionType>& types);

} // namespace ej::viewmodel
