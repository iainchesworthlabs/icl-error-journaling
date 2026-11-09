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
//   coveredTiles  the first this many tiles are under a card of the screen (the Log screen's empty board): a screen
//              reader skips them
//
// The signals say which tile and whether it is Other; the screen decides what a press and a tap do:
//   tilePressed(row, typeId, isOther)   the finger went down on a tile (or VoiceOver activated it): haptic, then record
//   tileTapped(row, typeId, isOther)    the finger came up inside the tile (Other opens the list of error types)
//   emptyTapped(position)               an empty position (1 to 11) was tapped: the screen opens the board setup there.
// Empty positions send neither of the first two, so a tap on one never records anything.
Item {
    id: root

    required property var model
    property bool skeleton: false
    property int coveredTiles: 0

    signal tilePressed(int row, string typeId, bool isOther)
    signal tileTapped(int row, string typeId, bool isOther)
    signal emptyTapped(int position)

    readonly property real tileWidth: (width + Theme.space.tileGap) / Theme.size.boardColumns - Theme.space.tileGap
    readonly property real boardHeight: (Theme.size.tileHeight + Theme.space.tileGap) * Theme.size.boardRows - Theme.space.tileGap

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
                covered: tile.index < root.coveredTiles
                onPressed: root.tilePressed(tile.index, tile.typeId, tile.isOther)
                onTapped: {
                    if (tile.isEmpty)
                        root.emptyTapped(tile.index + 1);
                    else
                        root.tileTapped(tile.index, tile.typeId, tile.isOther);
                }
            }
        }
    }
}
