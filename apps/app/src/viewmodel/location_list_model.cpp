// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/location_list_model.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

QVariant LocationListModel::data(const QModelIndex& index, int role) const
{
    const LocationRow* row = row_at(index);
    if (row == nullptr)
    {
        return {};
    }
    switch (role)
    {
    case LOCATION_ID:
        return QString::fromStdString(row->m_Id.format());
    case NAME:
    case Qt::DisplayRole:
        return row->m_Name;
    default:
        return {};
    }
}

QHash<int, QByteArray> LocationListModel::roleNames() const
{
    return {{LOCATION_ID, "locationId"}, {NAME, "name"}};
}

void LocationListModel::refresh(const Configuration& configuration)
{
    // The sort order decides; ties keep the order of the list.
    std::vector<const Location*> live;
    for (const Location& location : configuration.m_Locations)
    {
        if (!location.m_Retired)
        {
            live.push_back(&location);
        }
    }
    std::ranges::stable_sort(live, {}, &Location::m_SortOrder);

    std::vector<LocationRow> rows;
    rows.reserve(live.size());
    for (const Location* location : live)
    {
        rows.push_back({location->m_Id, QString::fromStdString(location->m_Name)});
    }
    replace_rows(std::move(rows));
}

} // namespace ej::viewmodel
