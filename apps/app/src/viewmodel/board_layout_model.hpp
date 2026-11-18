// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "viewmodel/board_model.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <array>

namespace ej::viewmodel
{

/// The miniature board of the settings screen (PRF-05): exactly 12 rows, board positions 1 to 11 and then Other, the
/// same shape as `BoardModel` without the counts. Row `p - 1` shows the live error type whose board position is `p`; a
/// position with no type is an empty tile, and row 11 is Other. The row count never changes and the model never
/// resets, so a tile being dragged is not interrupted: a change is one `dataChanged` over the smallest range of rows
/// that differ.
///
/// `POSITION` is the number `SettingsModel::moveBoardTile` and `assignBoardTile` take (1 to 11; Other is 12).
///
/// `SettingsModel` owns one and calls `refresh` after every change; it is read-only to QML.
class BoardLayoutModel final : public QAbstractListModel
{
    Q_OBJECT

public:
    /// The tiles of the board, Other included.
    static constexpr int ROWS = BOARD_ROWS;

    enum Role
    {
        TYPE_ID = Qt::UserRole + 1,
        LABEL,
        COLOUR_SLOT,
        POSITION,
        IS_OTHER,
        IS_EMPTY,
        ACCESSIBLE_NAME
    };
    // roleNames(): typeId, label, colourSlot, position, isOther, isEmpty, accessibleName

    /// Which board the miniature shows: the error types, or the types of the optional second board.
    enum class Board
    {
        ERRORS,
        INTERACTIONS
    };

    explicit BoardLayoutModel(QObject* parent = nullptr, Board board = Board::ERRORS);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Reads the layout from `configuration`.
    void refresh(const Configuration& configuration);

private:
    struct Tile
    {
        QString m_TypeId; // empty for an empty tile and for Other
        QString m_Label;  // empty for an empty tile
        int m_ColourSlot = NO_TILE_COLOUR;
        bool m_IsOther = false;

        friend bool operator==(const Tile&, const Tile&) = default;
    };

    Board m_Board;
    std::array<Tile, ROWS> m_Tiles;
};

} // namespace ej::viewmodel
