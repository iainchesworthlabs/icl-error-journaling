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
//   noteAction  there is a typed draft: each tile that can be held has a second VoiceOver element after the board,
//              "Log <name> with note" (BRD-07). They are made only while it is true, so a board with no draft pays one item
//              for them (perf.scene).
//
// The signals say which tile and whether it is Other; the screen decides what each does (a tile with a type is logged by a
// double-tap or a hold, never by one tap: see Tile):
//   tilePressed(row, typeId, isOther)   the tile asks for its entry: the second tap of a double-tap, a hold that nothing has
//                                       asked for yet, or VoiceOver activated it. The haptic, then the record. For Other the
//                                       finger went down on it, and the screen records nothing.
//   tileTapped(row, typeId, isOther)    the finger came up inside the tile (Other opens the list of error types)
//   tileHeld(row, typeId)               the finger stayed down on a tile for 500 ms after its tilePressed, or VoiceOver
//                                       chose "Log with note" (BRD-07). Other and empty positions are never held.
//   tileSingleTapped(row, typeId)       one tap, and no second within the double-tap window: nothing is logged, and the
//                                       screen shows the hint "Double-tap to log". Other and empty positions never send it.
//   emptyTapped(position)               an empty position (1 to 11) was tapped: the screen opens the board setup there.
// Empty positions send neither of the first two, so a tap on one never records anything.
Item {
    id: root

    required property var model
    property bool skeleton: false
    property int coveredTiles: 0
    property bool noteAction: false
    // The board is the Interactions board: tiles take the second hue family.
    property bool interactions: false
    // The object name of a tile is this and its row, so that the two boards of the Log screen have tiles that can be told apart.
    property string tilePrefix: "tile"

    signal tilePressed(int row, string typeId, bool isOther)
    signal tileHeld(int row, string typeId)
    signal tileTapped(int row, string typeId, bool isOther)
    signal tileSingleTapped(int row, string typeId)
    signal emptyTapped(int position)

    // The height of a tile and the gap between tiles: the phone's unless the screen gives others (the Log screen does, in
    // a larger window or a window too short for the phone's tiles; Stage 8).
    property real tileHeight: Theme.size.tileHeight
    property real gap: Theme.space.tileGap

    readonly property real tileWidth: (width + gap) / Theme.size.boardColumns - gap
    readonly property real boardHeight: (tileHeight + gap) * Theme.size.boardRows - gap

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: boardHeight

    Grid {
        id: grid

        objectName: "boardGrid"
        columns: Theme.size.boardColumns
        spacing: root.gap

        Repeater {
            id: tiles

            model: root.model

            delegate: Tile {
                id: tile

                required property int index
                // The model data of the row. The tile takes its roles by name, and the scene count (perf.scene) finds a
                // delegate by `index` and `model`.
                required property var model

                objectName: root.tilePrefix + tile.index
                width: root.tileWidth
                height: root.tileHeight
                skeleton: root.skeleton
                interactions: root.interactions
                covered: tile.index < root.coveredTiles
                onPressed: root.tilePressed(tile.index, tile.typeId, tile.isOther)
                onHeld: root.tileHeld(tile.index, tile.typeId)
                onSingleTapped: root.tileSingleTapped(tile.index, tile.typeId)
                onTapped: {
                    if (tile.isEmpty)
                        root.emptyTapped(tile.index + 1);
                    else
                        root.tileTapped(tile.index, tile.typeId, tile.isOther);
                }
            }
        }
    }

    // "Log <name> with note" for VoiceOver (BRD-07), one for each tile that can be held, while there is a draft. Qt has no
    // custom VoiceOver actions (U4), so each is an element of its own: not drawn, taking no touch, over its tile, and after
    // the board in the reading order. Activating one sends the press and then the hold, as a finger does.
    Repeater {
        id: noteActions

        model: root.noteAction && !root.skeleton ? root.model : 0

        delegate: Item {
            id: action

            required property int index
            required property string typeId
            required property string label
            required property bool isOther
            required property bool isEmpty

            objectName: "boardNoteAction"
            x: (index % Theme.size.boardColumns) * (root.tileWidth + root.gap)
            y: Math.floor(index / Theme.size.boardColumns) * (root.tileHeight + root.gap)
            width: root.tileWidth
            height: root.tileHeight
            // Not every row is offered: Other and an empty position cannot be held, and the empty-board card covers some.
            readonly property bool offered: !isOther && !isEmpty && index >= root.coveredTiles
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Log %1 with note").arg(label)
            Accessible.description: root.interactions ? qsTr("Logs this interaction with the note you typed") : qsTr("Logs this error with the note you typed")
            Accessible.ignored: !offered
            Accessible.focusable: offered
            Accessible.onPressAction: {
                root.tilePressed(index, typeId, false);
                root.tileHeld(index, typeId);
            }
        }
    }
}
