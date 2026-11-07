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

/// One row of `ErrorTypeListModel`.
struct ErrorTypeRow
{
    ErrorTypeId m_Id;
    QString m_Name;
    int m_ColourSlot = 1;    // 1 to 11, from the type's index in the master list
    int m_BoardPosition = 0; // 1 to 11, or 0 when the type is not on the board

    friend bool operator==(const ErrorTypeRow&, const ErrorTypeRow&) = default;
};

/// The error types master list of the settings screen (PRF-04): the types that are not retired, in master-list order.
/// A retired type is not a row (it has left the pickers), but it keeps its place in the master list, so the colour
/// slots of the others do not change when it goes (Stage 3, decision 8).
///
/// `SettingsModel` owns one and calls `refresh` after every change; it is read-only to QML. Edits go through the
/// invokables of `SettingsModel`.
class ErrorTypeListModel final : public RowListModel<ErrorTypeRow, ErrorTypeId>
{
    Q_OBJECT

public:
    enum Role
    {
        TYPE_ID = Qt::UserRole + 1,
        NAME,
        COLOUR_SLOT,
        BOARD_POSITION,
        ON_BOARD
    };
    // roleNames(): typeId, name, colourSlot, boardPosition, onBoard

    explicit ErrorTypeListModel(QObject* parent = nullptr) : RowListModel(parent) {}

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Rebuilds the rows from `configuration` and tells the views the least that is true.
    void refresh(const Configuration& configuration);

protected:
    [[nodiscard]] ErrorTypeId key_of(const ErrorTypeRow& row) const override
    {
        return row.m_Id;
    }
};

} // namespace ej::viewmodel
