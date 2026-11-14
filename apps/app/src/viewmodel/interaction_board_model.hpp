// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the Interactions board shows (Stage 14, package I-6): the 12 tiles of the second board, like `BoardModel` for
// the errors. Row `p - 1` is the live interaction type at board position `p` (the types of the setup file, or the
// built-in list when the file has none), row 11 is Other, which counts the interactions of types that are not on the
// board. The model has the same roles as `BoardModel`, so the same `BoardGrid` draws it, and the same rules: 12 rows
// for good, one `dataChanged` for a commit, nothing for a note or a header change.
//
// It records nothing: a tap is `LogSession::recordInteractionEntry`.
//
// GUI thread only. Destroy it before the sources it was given and before the open document.

#include "viewmodel/board_model.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/open_log_source.hpp"

#include <QAbstractListModel>
#include <QString>
#include <QVariantList>
#include <array>

namespace ej::viewmodel
{

class InteractionBoardModel final : public QAbstractListModel, private ConfigurationObserver
{
    Q_OBJECT
    /// No interaction type is on the board: the screen shows the empty state and the way to the setup.
    Q_PROPERTY(bool boardEmpty READ board_empty NOTIFY boardEmptyChanged)
    /// The types that are in force and not on the board, as rows {typeId, label}, for the Other list.
    Q_PROPERTY(QVariantList otherTypes READ other_types NOTIFY otherTypesChanged)

public:
    enum Role
    {
        TYPE_ID = Qt::UserRole + 1,
        LABEL,
        COLOUR_SLOT,
        COUNT,
        HAS_COUNT,
        IS_OTHER,
        IS_EMPTY,
        ACCESSIBLE_NAME
    };

    InteractionBoardModel(ConfigurationSource& configuration, OpenLogSource& open_log, QObject* parent = nullptr);
    ~InteractionBoardModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    [[nodiscard]] bool board_empty() const;
    [[nodiscard]] QVariantList other_types() const;

    /// What the type asks for after it is logged: "nothing", "person" or "text". "nothing" for a type not known.
    Q_INVOKABLE QString asksFor(const QString& typeId) const;
    /// The type has an answer slot ({value}) in its line: after the people, the answer is asked for.
    Q_INVOKABLE bool hasValueSlot(const QString& typeId) const;
    /// The name of the type, or empty.
    Q_INVOKABLE QString typeName(const QString& typeId) const;
    /// The line the entry will have, with the answer typed so far and no people ("Played Morning Medley").
    Q_INVOKABLE QString previewLine(const QString& typeId, const QString& value) const;

signals:
    void boardEmptyChanged();
    void otherTypesChanged();

private:
    struct Tile
    {
        QString m_TypeId;
        QString m_Label;
        int m_ColourSlot = NO_TILE_COLOUR;
        int m_Count = 0;
        bool m_IsOther = false;

        friend bool operator==(const Tile&, const Tile&) = default;
    };
    using Tiles = std::array<Tile, BOARD_ROWS>;

    void on_configuration_changed(const ConfigurationSource& source) override;
    void on_open_log_event(const LogChange& change);

    [[nodiscard]] Tiles build() const;
    void refresh_everything();
    void refresh_counts();

    ConfigurationSource& m_Configuration;
    Tiles m_Tiles;
    OpenLogFollower m_Log;
};

} // namespace ej::viewmodel
