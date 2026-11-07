// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/board_layout_model.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace ej::viewmodel
{

BoardLayoutModel::BoardLayoutModel(QObject* parent) : QAbstractListModel(parent)
{
    m_Tiles.back().m_IsOther = true;
}

int BoardLayoutModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ROWS;
}

QVariant BoardLayoutModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid() || index.row() < 0 || index.row() >= ROWS)
    {
        return {};
    }
    const Tile& tile = m_Tiles[static_cast<std::size_t>(index.row())];
    switch (role)
    {
    case TYPE_ID:
        return tile.m_TypeId;
    case LABEL:
    case Qt::DisplayRole:
        return tile.m_IsOther ? tr("Other") : tile.m_Label;
    case COLOUR_SLOT:
        return tile.m_ColourSlot;
    case POSITION:
        return index.row() + 1;
    case IS_OTHER:
        return tile.m_IsOther;
    case IS_EMPTY:
        return !tile.m_IsOther && tile.m_TypeId.isEmpty();
    case ACCESSIBLE_NAME:
        if (tile.m_IsOther)
        {
            return tr("Other");
        }
        if (tile.m_TypeId.isEmpty())
        {
            return tr("Empty tile, position %1").arg(index.row() + 1);
        }
        return tile.m_Label;
    default:
        return {};
    }
}

QHash<int, QByteArray> BoardLayoutModel::roleNames() const
{
    return {{TYPE_ID, "typeId"},   {LABEL, "label"},      {COLOUR_SLOT, "colourSlot"},        {POSITION, "position"},
            {IS_OTHER, "isOther"}, {IS_EMPTY, "isEmpty"}, {ACCESSIBLE_NAME, "accessibleName"}};
}

void BoardLayoutModel::refresh(const Configuration& configuration)
{
    std::array<Tile, ROWS> next;
    next.back().m_IsOther = true;
    for (std::size_t i = 0; i < configuration.m_ErrorTypes.size(); ++i)
    {
        const ErrorType& type = configuration.m_ErrorTypes[i];
        if (type.m_Retired || !type.m_BoardPosition || *type.m_BoardPosition < 1 ||
            *type.m_BoardPosition > CONFIG_BOARD_POSITIONS)
        {
            continue;
        }
        Tile& tile = next[static_cast<std::size_t>(*type.m_BoardPosition - 1)];
        tile.m_TypeId = QString::fromStdString(type.m_Id.format());
        tile.m_Label = QString::fromStdString(type.m_Name);
        tile.m_ColourSlot = colour_slot_for_index(i);
    }

    int first = ROWS;
    int last = -1;
    for (int row = 0; row < ROWS; ++row)
    {
        if (!(m_Tiles[static_cast<std::size_t>(row)] == next[static_cast<std::size_t>(row)]))
        {
            first = std::min(first, row);
            last = row;
        }
    }
    m_Tiles = std::move(next);
    if (last >= 0)
    {
        emit dataChanged(index(first), index(last));
    }
}

} // namespace ej::viewmodel
