// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/interaction_board_model.hpp"

namespace ej::viewmodel
{

InteractionBoardModel::InteractionBoardModel(QObject* parent) : QObject(parent) {}

bool InteractionBoardModel::board_empty() const
{
    return true;
}

} // namespace ej::viewmodel
