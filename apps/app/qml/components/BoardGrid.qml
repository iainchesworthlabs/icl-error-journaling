// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The capture board (Stage 4, package 4.5): three columns and four rows of Tile over the 12 rows of a BoardModel, in the
// exact geometry of the design. At the width of the Log screen, 402 - 2 x 16 = 370 pt, a column is
// (370 - 2 x 8) / 3 = 118 pt, a tile is 104 pt high, the gaps are 8 pt, and the board is 440 pt tall. It never scrolls and
// it never changes shape: the model has 12 rows for good and never resets, so a change in one row changes one tile and a
// press animation running on another tile is not touched.
//
//   model      the BoardModel (roles typeId, label, colourSlot, count, hasCount, isOther, isEmpty, accessibleName)
//   skeleton   draw the 12 shapes only, in neutral colours, with no text, count or type colour (the launch screen)
//
// The signals say which tile and whether it is Other; the screen decides what a press and a tap do:
//   tilePressed(row, typeId, isOther)   the finger went down on a tile (or VoiceOver activated it): haptic, then record
//   tileTapped(row, typeId, isOther)    the finger came up inside the tile (Other opens the list of error types)
// Empty positions send neither.
Item {
    id: root

    required property var model
    property bool skeleton: false

    signal tilePressed(int row, string typeId, bool isOther)
    signal tileTapped(int row, string typeId, bool isOther)

    readonly property real tileWidth: (width - (Theme.size.boardColumns - 1) * Theme.space.tileGap) / Theme.size.boardColumns
    readonly property real boardHeight: Theme.size.boardRows * Theme.size.tileHeight + (Theme.size.boardRows - 1) * Theme.space.tileGap

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: boardHeight

    Grid {
        id: grid

        objectName: "boardGrid"
        columns: Theme.size.boardColumns
        spacing: Theme.space.tileGap

        Repeater {
            id: tiles

            model: root.model

            delegate: Tile {
                id: tile

                required property int index
                // The model data of the row. The tile takes its roles by name, and the scene count (perf.scene) finds a
                // delegate by `index` and `model`.
                required property var model

                objectName: "tile" + index
                width: root.tileWidth
                height: Theme.size.tileHeight
                skeleton: root.skeleton
                onPressed: root.tilePressed(tile.index, tile.typeId, tile.isOther)
                onTapped: root.tileTapped(tile.index, tile.typeId, tile.isOther)
            }
        }
    }
}
