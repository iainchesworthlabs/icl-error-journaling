// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/board_model.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/open_log_source.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <vector>

namespace ej::viewmodel
{

/// The Other sheet (BRD-06): every live (not retired) error type of the master list, with a search field, in two
/// sections, "Not on your board" first and "On your board" after it, each in master-list order. A row carries today's
/// count from the open log and the type's colour slot, which is the tile's rule (`tile_colour_slot`).
///
/// Picking a type is `LogSession::recordError(typeId)`, the same path as a tile; the model records nothing. `addType`
/// adds a type to the master list through `ConfigurationEditor` and does not record an entry either: the type appears
/// under "Not on your board" once the save has committed, and the facilitator taps it as any other (Stage 3, decision
/// 12), so a typo in a new name cannot log an entry she did not mean to.
///
/// The filter is a case-insensitive substring match on the label (`QString::toCaseFolded`, with the filter text trimmed
/// of leading and trailing white space). It does not fold accents. A change of the filter rebuilds the list with one
/// `modelReset`. A commit that changes today's counts is one `dataChanged` over the smallest range of rows, role
/// `COUNT`; a change of the configuration that adds, removes, renames or moves a type between the sections is one
/// `modelReset`, and one that only changes counts is the `dataChanged`.
///
/// GUI thread only. The model must be destroyed before the sources and the editor it was given and before the open
/// document.
class ErrorPickerModel final : public QAbstractListModel, private ConfigurationObserver
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE set_filter NOTIFY filterChanged)
    /// Why the last `addType` was refused, in plain English for the sheet to show inline; empty after a success and
    /// before the first call.
    Q_PROPERTY(QString addTypeError READ add_type_error NOTIFY addTypeErrorChanged)

public:
    enum Section
    {
        OFF_BOARD, // "Not on your board"
        ON_BOARD   // "On your board"
    };
    Q_ENUM(Section)

    enum Role
    {
        TYPE_ID = Qt::UserRole + 1,
        LABEL,
        SECTION,
        COUNT,
        COLOUR_SLOT
    };
    // roleNames(): typeId, label, section, count, colourSlot

    ErrorPickerModel(ConfigurationSource& configuration, OpenLogSource& open_log, ConfigurationEditor& editor,
                     QObject* parent = nullptr);
    ~ErrorPickerModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] QString filter() const
    {
        return m_Filter;
    }
    void set_filter(const QString& filter);

    [[nodiscard]] QString add_type_error() const
    {
        return m_AddTypeError;
    }

    /// Adds a type named `name` to the end of the master list (PRF-04) and returns its id, or an empty string when the
    /// rules refuse it (an empty, duplicate or over-long name, a full list) or the store is not ready; `addTypeError`
    /// then says why. The type is in this model only after the save has committed.
    Q_INVOKABLE QString addType(const QString& name);

signals:
    void filterChanged();
    void addTypeErrorChanged();

private:
    struct Row
    {
        QString m_TypeId;
        QString m_Label;
        Section m_Section = OFF_BOARD;
        int m_Count = 0;
        int m_ColourSlot = NO_TILE_COLOUR;

        friend bool operator==(const Row&, const Row&) = default;
    };

    void on_configuration_changed(const ConfigurationSource& source) override;
    void on_open_log_event(const LogChange& change);

    [[nodiscard]] std::vector<Row> build() const;
    void refresh();
    void set_add_type_error(QString message);

    ConfigurationSource& m_Configuration;
    ConfigurationEditor& m_Editor;
    QString m_Filter;
    QString m_AddTypeError;
    std::vector<Row> m_Rows;
    OpenLogFollower m_Log;
};

} // namespace ej::viewmodel
