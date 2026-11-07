// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "error_journaling/ids.hpp"
#include "viewmodel/row_list_model.hpp"

#include <QByteArray>
#include <QHash>
#include <QString>
#include <QVariant>

namespace ej::viewmodel
{

/// One row of `LocationListModel`.
struct LocationRow
{
    LocationId m_Id;
    QString m_Name;

    friend bool operator==(const LocationRow&, const LocationRow&) = default;
};

/// The locations of the settings screen (PRF-03): the locations that are not retired, in their sort order. "Remove"
/// retires a location, so a past log still names it (Stage 3, accepted defaults); a retired location is not a row.
///
/// `SettingsModel` owns one and calls `refresh` after every change; it is read-only to QML.
class LocationListModel final : public RowListModel<LocationRow, LocationId>
{
    Q_OBJECT

public:
    enum Role
    {
        LOCATION_ID = Qt::UserRole + 1,
        NAME
    };
    // roleNames(): locationId, name

    explicit LocationListModel(QObject* parent = nullptr) : RowListModel(parent) {}

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Rebuilds the rows from `configuration` and tells the views the least that is true.
    void refresh(const Configuration& configuration);

protected:
    [[nodiscard]] LocationId key_of(const LocationRow& row) const override
    {
        return row.m_Id;
    }
};

} // namespace ej::viewmodel
