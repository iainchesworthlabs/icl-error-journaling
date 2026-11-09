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
        // The mini board's tile reads "Other... (fixed)" in the designs (BoardSettings, BoardLayoutSelected): it opens
        // nothing here and cannot be moved. The accessible name keeps the plain word.
        return tile.m_IsOther ? tr("Other\xE2\x80\xA6 (fixed)") : tile.m_Label;
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
            return tr("Position %1, empty. Add error type").arg(index.row() + 1);
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
    const BoardLayout layout = board_layout(configuration);
    for (std::size_t position = 0; position < layout.size(); ++position)
    {
        if (!layout[position])
        {
            continue;
        }
        const ErrorType& type = configuration.m_ErrorTypes[*layout[position]];
        Tile& tile = next[position];
        tile.m_TypeId = QString::fromStdString(type.m_Id.format());
        tile.m_Label = QString::fromStdString(type.m_Name);
        tile.m_ColourSlot = tile_colour_slot(*layout[position]);
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
