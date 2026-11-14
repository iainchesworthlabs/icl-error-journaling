// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The interaction types in force (Stage 14, package I-6): the types of the setup file, or, when the file has none, the
// built-in list (`default_interaction_types`), so that the Interactions board shows tiles with no setup at all.

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
