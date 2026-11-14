// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/interaction_types.hpp"

namespace ej::viewmodel
{

std::vector<InteractionType> interaction_types_in_force(const Configuration& configuration)
{
    if (configuration.m_InteractionTypes.empty())
    {
        return default_interaction_types();
    }
    return configuration.m_InteractionTypes;
}

InteractionLayout interaction_layout(const std::vector<InteractionType>& types)
{
    InteractionLayout layout;
    for (std::size_t i = 0; i < types.size(); ++i)
    {
        const InteractionType& type = types[i];
        if (type.m_Retired || !type.m_BoardPosition || *type.m_BoardPosition < 1 ||
            *type.m_BoardPosition > CONFIG_BOARD_POSITIONS)
        {
            continue;
        }
        auto& slot = layout[static_cast<std::size_t>(*type.m_BoardPosition - 1)];
        if (!slot)
        {
            slot = i;
        }
    }
    return layout;
}

} // namespace ej::viewmodel
