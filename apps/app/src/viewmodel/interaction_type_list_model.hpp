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

/// One row of `InteractionTypeListModel`.
struct InteractionTypeRow
{
    InteractionTypeId m_Id;
    QString m_Name;
    QString m_Line;          // the line as the field shows it: "Spoke with [person]"; empty when the type has none
    QString m_Asks;          // "nothing", "person" or "text"
    int m_BoardPosition = 0; // 1 to 11, or 0 when the type is not on the board

    friend bool operator==(const InteractionTypeRow&, const InteractionTypeRow&) = default;
};

/// The interaction types of the settings screen (Stage 14, 4.5): the types that are not retired, in master-list order.
/// Read-only to QML; edits go through the invokables of `SettingsModel`.
class InteractionTypeListModel final : public RowListModel<InteractionTypeRow, InteractionTypeId>
{
    Q_OBJECT

public:
    enum Role
    {
        TYPE_ID = Qt::UserRole + 1,
        NAME,
        LINE,
        ASKS,
        BOARD_POSITION,
        ON_BOARD
    };
    // roleNames(): typeId, name, line, asks, boardPosition, onBoard

    explicit InteractionTypeListModel(QObject* parent = nullptr) : RowListModel(parent) {}

    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    void refresh(const Configuration& configuration);

    /// The stored pattern as the "Line in the log" field shows it: {person} is "[person]", {value} is "[answer]".
    [[nodiscard]] static QString pattern_to_field(const QString& pattern);
    /// The stored pattern for what the field shows.
    [[nodiscard]] static QString pattern_from_field(const QString& field);

protected:
    [[nodiscard]] InteractionTypeId key_of(const InteractionTypeRow& row) const override
    {
        return row.m_Id;
    }
};

} // namespace ej::viewmodel
