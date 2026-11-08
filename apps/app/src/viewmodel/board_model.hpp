// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"
#include "viewmodel/configuration_source.hpp"
#include "viewmodel/log_observer.hpp"
#include "viewmodel/move_only_function.hpp"
#include "viewmodel/open_log_source.hpp"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QString>
#include <QVariant>
#include <array>
#include <cstddef>
#include <optional>
#include <unordered_map>

namespace ej::viewmodel
{

// What `BoardModel` and `ErrorPickerModel` share: the shape of the board, the colour rule, the count of the live
// entries and the follower of the open log.

/// Tiles on the board: the positions 1 to 11 and then Other.
inline constexpr int BOARD_ROWS = CONFIG_BOARD_POSITIONS + 1;

/// The row of Other, the twelfth tile.
inline constexpr int OTHER_ROW = CONFIG_BOARD_POSITIONS;

/// The tile colours of the design tokens (`docs/design/tokens.json`, slots 1 to 11). Other and an empty tile have no
/// type colour: slot 0.
inline constexpr int TILE_COLOUR_SLOTS = 11;
inline constexpr int NO_TILE_COLOUR = 0;

/// The colour slot of the type at `master_index` in the master list (`Configuration::m_ErrorTypes`, retired types
/// included): the index modulo 11, plus one, so slots 1 to 11 and the 12th and 13th types reuse slots 1 and 2. The
/// colour belongs to the type and not to its board position, so moving a tile never changes it (Stage 3, decision 8).
[[nodiscard]] constexpr int tile_colour_slot(std::size_t master_index) noexcept
{
    return static_cast<int>(master_index % static_cast<std::size_t>(TILE_COLOUR_SLOTS)) + 1;
}

/// The name of a tile colour slot, as the error type editor shows it ("Periwinkle" for slot 8; the names are those of
/// `docs/design/tokens.json`). Empty for slot 0 (Other, an empty tile) and for a slot outside 1 to 11.
[[nodiscard]] QString tile_colour_name(int slot);

/// The master-list index of the live type on each board position (index `p - 1` for position `p`), or nothing for an
/// empty position. A retired type is left out of the layout, a position outside 1 to 11 is not a position, and if two
/// live types claim one position the first in the master list has it and the other is off the board.
using BoardLayout = std::array<std::optional<std::size_t>, CONFIG_BOARD_POSITIONS>;
[[nodiscard]] BoardLayout board_layout(const Configuration& configuration);

/// Live (not deleted) error entries of the log, by type. Types the configuration does not know are in the map too.
/// One pass over the entries, where `Log::count_for` is one pass per type.
[[nodiscard]] std::unordered_map<ErrorTypeId, int> live_error_counts(const Log& log);

class LogDocument; // work package 3.2

/// Follows the open log for a model: it is told by the `OpenLogSource` when another document takes its place, observes
/// that document, and passes on what happens to it. Only the open document is followed, so the archived log of a past
/// day, which History and Review hold, never reaches the model.
///
/// The change handler is called for every `LogChange` of the open document, and with `REPLACED` when the open
/// document itself is another (or none). The source must tell its observers before it lets go of the old document,
/// because the follower takes itself off the old document then.
class OpenLogFollower final : private OpenLogObserver, private LogObserver
{
public:
    using Changed = MoveOnlyFunction<void(const LogChange&)>;

    /// Starts on the source's current document without calling `changed`; the owner reads `document()` itself.
    OpenLogFollower(OpenLogSource& source, Changed changed);
    ~OpenLogFollower() override;

    OpenLogFollower(const OpenLogFollower&) = delete;
    OpenLogFollower& operator=(const OpenLogFollower&) = delete;

    /// The open document, or null while there is none.
    [[nodiscard]] const LogDocument* document() const
    {
        return m_Document;
    }

private:
    void on_open_log_changed(LogDocument* document) override;
    void on_log_changed(const LogDocument& document, const LogChange& change) override;

    void attach(LogDocument* document);
    void detach();

    OpenLogSource& m_Source;
    Changed m_Changed;
    LogDocument* m_Document = nullptr;
};

/// The capture board (BRD-01, BRD-05, BRD-08): exactly 12 rows, the positions 1 to 11 and then Other. The row count
/// never changes, so the grid cannot jump, and the model never resets or changes its layout, so a press animation that
/// is running on a tile is not interrupted.
///
/// Row `p - 1` shows the live error type whose board position is `p`. A position with no type is an empty tile
/// (`isEmpty`, no label, count 0) and the screen never taps it. Row 11 is Other (`isOther`, no `typeId`, colour slot
/// 0): its count is every live error entry of a type that is not on the board, which includes retired types, types
/// moved off the board and types the configuration does not know (BRD-05).
///
/// The tile colour is derived from the type's place in the master list, not from its board position, so reordering the
/// board does not recolour a type (BRD-08).
///
/// The model reads the configuration only through `ConfigurationSource` and the open log only through `OpenLogSource`.
/// It records nothing: a tap is `LogSession::recordError(typeId)` (Stage 3, 3.4), and the impact haptic that goes with
/// it is the touch-down acknowledgement that QML or the rig makes, not the model.
///
/// Signals. One commit that changes the counts is one `dataChanged` over the smallest range of rows that changed, with
/// the roles `COUNT`, `HAS_COUNT` and `ACCESSIBLE_NAME`; a commit that changes no tile (a note, a header change, a
/// share) emits nothing, and a failed write tells nobody. A new open log, a new day and a configuration change are one
/// `dataChanged` over rows 0 to 11 with every role (an empty role list, which Qt reads as all).
///
/// GUI thread only. The model must be destroyed before the sources it was given and before the open document.
class BoardModel final : public QAbstractListModel, private ConfigurationObserver
{
    Q_OBJECT
    Q_PROPERTY(int totalCount READ total_count NOTIFY countsChanged)

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
    // roleNames(): typeId, label, colourSlot, count, hasCount, isOther, isEmpty, accessibleName

    BoardModel(ConfigurationSource& configuration, OpenLogSource& open_log, QObject* parent = nullptr);
    ~BoardModel() override;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    /// Live error entries in the open log, Other's included; 0 while there is no open log.
    [[nodiscard]] int total_count() const
    {
        return m_Total;
    }

signals:
    /// The counts changed after a commit, a new log or a configuration change. `totalCount` is read again.
    void countsChanged();

private:
    struct Tile
    {
        QString m_TypeId; // empty for an empty tile and for Other
        QString m_Label;  // empty for an empty tile; Other's label is added when the data is read
        int m_ColourSlot = NO_TILE_COLOUR;
        int m_Count = 0;
        bool m_IsOther = false;

        friend bool operator==(const Tile&, const Tile&) = default;
    };
    using Tiles = std::array<Tile, BOARD_ROWS>;

    struct Built
    {
        Tiles m_Tiles;
        int m_Total = 0;
    };

    void on_configuration_changed(const ConfigurationSource& source) override;
    void on_open_log_event(const LogChange& change);

    [[nodiscard]] Built build() const;
    void refresh_everything();
    void refresh_counts();

    ConfigurationSource& m_Configuration;
    Tiles m_Tiles;
    int m_Total = 0;
    OpenLogFollower m_Log;
};

} // namespace ej::viewmodel
