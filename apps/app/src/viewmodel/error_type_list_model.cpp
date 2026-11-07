// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/error_type_list_model.hpp"

#include "viewmodel/colour_slot.hpp"

#include <cstddef>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

QVariant ErrorTypeListModel::data(const QModelIndex& index, int role) const
{
    const ErrorTypeRow* row = row_at(index);
    if (row == nullptr)
    {
        return {};
    }
    switch (role)
    {
    case TYPE_ID:
        return QString::fromStdString(row->m_Id.format());
    case NAME:
    case Qt::DisplayRole:
        return row->m_Name;
    case COLOUR_SLOT:
        return row->m_ColourSlot;
    case BOARD_POSITION:
        return row->m_BoardPosition;
    case ON_BOARD:
        return row->m_BoardPosition != 0;
    default:
        return {};
    }
}

QHash<int, QByteArray> ErrorTypeListModel::roleNames() const
{
    return {{TYPE_ID, "typeId"},
            {NAME, "name"},
            {COLOUR_SLOT, "colourSlot"},
            {BOARD_POSITION, "boardPosition"},
            {ON_BOARD, "onBoard"}};
}

void ErrorTypeListModel::refresh(const Configuration& configuration)
{
    std::vector<ErrorTypeRow> rows;
    for (std::size_t i = 0; i < configuration.m_ErrorTypes.size(); ++i)
    {
        const ErrorType& type = configuration.m_ErrorTypes[i];
        if (!type.m_Retired)
        {
            rows.push_back({type.m_Id, QString::fromStdString(type.m_Name), colour_slot_for_index(i),
                            type.m_BoardPosition.value_or(0)});
        }
    }
    replace_rows(std::move(rows));
}

} // namespace ej::viewmodel
