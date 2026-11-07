// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// The miniature board of the settings screen (Stage 4, package 4.5; PRF-02, PRF-05): the 12 tiles of the board in small, so
// that the facilitator can arrange them, and the means to do it. It shows a BoardLayoutModel and changes nothing; every
// change is a signal, and the board changes only when the model does. A screen connects the signals to SettingsModel.
//
//   model              the BoardLayoutModel (roles typeId, label, colourSlot, position, isOther, isEmpty, accessibleName)
//   selectedPosition   the selected tile's position, 1 to 11, or 0 for none; a tap sets it, Done clears it
//   dragging           a tile is being dragged (a screen turns scrolling off while it is, so the page does not move)
//
//   moveRequested(fromPosition, toPosition)   settings.moveBoardTile(from, to): a drop, or Move earlier and Move later
//   replaceRequested(position)                the screen lets the facilitator choose a type, then calls
//                                             settings.assignBoardTile(position, typeId)
//
// Hold a tile and drag it to move it. The held tile lifts and follows the finger, and on release the board asks for the
// move: nothing moves until the model says so, and when it does not (a refusal) the held tile is back in its place. That is
// a shortcut for sighted users. Tap a tile and a panel under the board names it and has Move earlier, Move later and
// Replace type, which do the same without a gesture and are the VoiceOver path (docs/plan/stage-4.md, "No gesture is the
// only way in"). The selected tile has an outline, a lift and a tick, so it is not shown by colour alone, and the selection
// follows the tile when the model moves it. Other is fixed: it is shown and named, and neither selected nor moved. An empty
// position can be selected, to put a type in it.
//
// The sizes are those of the design (64 pt tiles, 6 pt gaps, 11.5 pt labels, a lift of 1.04) from Theme.miniBoard.
Item {
    id: root

    required property var model
    property int selectedPosition: 0
    readonly property alias dragging: dragState.active

    signal moveRequested(int fromPosition, int toPosition)
    signal replaceRequested(int position)

    readonly property real gap: Theme.miniBoard.gap
    readonly property real tileWidth: (width + gap) / Theme.size.boardColumns - gap
    readonly property real tileHeight: Theme.miniBoard.tile
    readonly property real labelPixels: Math.round(Theme.miniBoard.labelSize * Theme.font.tileLabel.scale)
    readonly property real liftScale: Theme.miniBoard.selectedScale
    readonly property real liftOffset: Theme.space.tileGap / 2
    readonly property real outlineWidth: Theme.border.otherWidth * 4 / 3
    // Tiles that can be moved: the positions 1 to 11 (the last row, Other, is fixed).
    readonly property int positions: tiles.count - 1
    readonly property var selectedTile: tiles.count > 0 && selectedPosition >= 1 ? tiles.itemAt(selectedPosition - 1) : null

    // The selected tile is followed when the model moves it: the type that was asked to move, and where it should arrive.
    property string followTypeId: ""
    property int followPosition: 0

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: column.implicitHeight

    function select(position: int): void {
        selectedPosition = position;
    }

    // Asks for a move and makes the selection follow the tile if the model has moved it by the time this returns.
    function requestMove(fromPosition: int, toPosition: int): void {
        const tile = tileItem(fromPosition - 1);
        if (tile && root.selectedPosition === fromPosition) {
            followTypeId = tile.typeId;
            followPosition = toPosition;
        }
        moveRequested(fromPosition, toPosition);
        followTypeId = "";
    }

    // The delegate of a row, untyped so that a function can read the roles it holds.
    function tileItem(row: int): var {
        return tiles.itemAt(row);
    }

    function tileAt(x: real, y: real): var {
        const item = grid.childAt(x, y);
        return item && item.objectName.startsWith("miniTile") ? item : null;
    }

    // The hold that turns a press into a drag, as long as the platform makes a press-and-hold.
    function beginDrag(): void {
        const source = dragState.pressTile;
        if (!source || !source.movable)
            return;
        dragState.source = source;
        dragState.target = source;
        dragState.offsetX = dragState.pressX - source.x;
        dragState.offsetY = dragState.pressY - source.y;
        proxy.x = source.x;
        proxy.y = source.y;
        dragState.holding = false;
        dragState.active = true;
    }

    // The finger came up. The move is asked for and nothing else changes: the model moves the tile, or does not, and the
    // held tile is drawn where the model has it.
    function drop(): void {
        const source = dragState.source;
        const target = dragState.target;
        dragState.swallowTap = true;
        dragState.active = false;
        dragState.source = null;
        dragState.target = null;
        if (source && target && target !== source)
            requestMove(source.position, target.position);
    }

    // One drag, from the press to the release.
    QtObject {
        id: dragState

        property bool active: false
        property var source: null
        property var target: null
        property real offsetX: 0
        property real offsetY: 0
        // A finger is down on a tile that can be held, and has not moved enough to be a scroll.
        property bool holding: false
        property var pressTile: null
        property real pressX: 0
        property real pressY: 0
        // Set when a drag ends, so that the tap handler of the tile it ended on does not also select it.
        property bool swallowTap: false
    }

    Timer {
        id: hold

        interval: Application.styleHints.mousePressAndHoldInterval
        onTriggered: root.beginDrag()
    }

    component PanelButton: Rectangle {
        id: button

        required property string label
        required property string glyph

        signal activated

        height: Math.max(Theme.size.primaryButtonHeight, content.implicitHeight + 2 * Theme.space.tileGap)
        radius: Theme.radius.field
        color: Theme.colour.primaryTint
        opacity: enabled ? 1 : 0.4
        Accessible.role: Accessible.Button
        Accessible.name: button.label
        Accessible.focusable: true
        Accessible.onPressAction: {
            if (button.enabled)
                button.activated();
        }

        Column {
            id: content

            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space.tileGap
            spacing: Theme.border.otherWidth

            Item {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.size.badge * 0.85
                height: width
                Accessible.ignored: true

                Shape {
                    width: Theme.size.badge
                    height: Theme.size.badge
                    scale: parent.width / Theme.size.badge
                    transformOrigin: Item.TopLeft

                    ShapePath {
                        strokeColor: Theme.colour.primaryText
                        strokeWidth: Theme.border.otherWidth * 4 / 3
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin

                        PathSvg {
                            path: button.glyph
                        }
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: button.label
                color: Theme.colour.primaryText
                font: Theme.font.caption.selectedFont
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }
        }

        TapHandler {
            enabled: button.enabled
            onTapped: button.activated()
        }
    }

    Column {
        id: column

        width: parent.width
        spacing: Theme.space.sectionGap

        Grid {
            id: grid

            objectName: "miniGrid"
            columns: Theme.size.boardColumns
            spacing: root.gap
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Your board")

            Repeater {
                id: tiles

                model: root.model

                delegate: Item {
                    id: tile

                    required property int index
                    required property var model
                    required property string typeId
                    required property string label
                    required property int colourSlot
                    required property int position
                    required property bool isOther
                    required property bool isEmpty
                    required property string accessibleName

                    readonly property bool selected: root.selectedPosition === position && !isOther
                    readonly property bool movable: !isOther && !isEmpty
                    readonly property bool lifted: dragState.active && dragState.source === tile

                    objectName: "miniTile" + index
                    width: root.tileWidth
                    height: root.tileHeight
                    Accessible.role: tile.isOther ? Accessible.StaticText : Accessible.Button
                    Accessible.name: tile.accessibleName
                    Accessible.checkable: !tile.isOther
                    Accessible.checked: tile.selected
                    Accessible.focusable: true
                    Accessible.onPressAction: {
                        if (!tile.isOther)
                            root.select(tile.position);
                    }

                    // The model put the type that was asked to move where it was asked to go: the selection goes with it.
                    onTypeIdChanged: {
                        if (root.followTypeId !== "" && tile.typeId === root.followTypeId && tile.position === root.followPosition) {
                            root.followTypeId = "";
                            root.selectedPosition = tile.position;
                        }
                    }

                    Item {
                        id: face

                        objectName: "miniFace" + tile.index
                        width: parent.width
                        height: parent.height
                        scale: tile.selected ? root.liftScale : 1
                        y: tile.selected ? -root.liftOffset : 0
                        opacity: tile.lifted ? 0.35 : 1
                        z: tile.selected ? 1 : 0

                        Rectangle {
                            objectName: "miniBackground"
                            anchors.fill: parent
                            visible: !tile.isEmpty && !tile.isOther
                            radius: Theme.radius.field
                            color: Theme.tileColour(tile.colourSlot)
                            border.width: tile.selected ? root.outlineWidth : 0
                            border.color: Theme.colour.primaryText
                            Accessible.ignored: true
                        }

                        Rectangle {
                            objectName: "miniEmptySlot"
                            anchors.fill: parent
                            visible: tile.isEmpty
                            radius: Theme.radius.field
                            color: "transparent"
                            border.width: tile.selected ? root.outlineWidth : Theme.border.hairline
                            border.color: tile.selected ? Theme.colour.primaryText : Theme.colour.border
                            Accessible.ignored: true
                        }

                        Shape {
                            objectName: "miniOtherBorder"
                            anchors.fill: parent
                            visible: tile.isOther

                            ShapePath {
                                fillColor: Theme.colour.otherTileFill
                                strokeColor: Theme.colour.otherTileBorder
                                strokeWidth: Theme.border.otherWidth
                                strokeStyle: ShapePath.DashLine
                                dashPattern: [3, 2]
                                capStyle: ShapePath.FlatCap

                                PathRectangle {
                                    x: Theme.border.otherWidth / 2
                                    y: Theme.border.otherWidth / 2
                                    width: tile.width - Theme.border.otherWidth
                                    height: tile.height - Theme.border.otherWidth
                                    radius: Theme.radius.field
                                }
                            }
                        }

                        Text {
                            objectName: "miniLabel"
                            anchors.fill: parent
                            anchors.margins: root.gap
                            visible: !tile.isEmpty
                            text: tile.label
                            color: Theme.colour.tileText
                            font.pixelSize: root.labelPixels
                            font.weight: Theme.font.tileLabel.weight
                            wrapMode: Text.Wrap
                            verticalAlignment: Text.AlignBottom
                            fontSizeMode: Text.Fit
                            minimumPixelSize: Math.round(root.labelPixels * 0.6)
                            Accessible.ignored: true
                        }

                        // The tick that says "selected" without colour.
                        Rectangle {
                            objectName: "miniTick"
                            anchors.top: parent.top
                            anchors.right: parent.right
                            anchors.margins: Theme.space.tileGap / 2
                            width: Theme.size.badge * 0.75
                            height: width
                            radius: width / 2
                            visible: tile.selected
                            color: Theme.colour.primaryText
                            Accessible.ignored: true

                            Shape {
                                width: Theme.size.badge
                                height: Theme.size.badge
                                scale: parent.width / Theme.size.badge
                                transformOrigin: Item.TopLeft

                                ShapePath {
                                    strokeColor: Theme.colour.background
                                    strokeWidth: Theme.border.otherWidth * 5 / 3
                                    fillColor: "transparent"
                                    capStyle: ShapePath.RoundCap
                                    joinStyle: ShapePath.RoundJoin

                                    PathSvg {
                                        path: "M5 12l5 5L20 7"
                                    }
                                }
                            }
                        }
                    }

                    TapHandler {
                        objectName: "miniTap"
                        enabled: !tile.isOther
                        onTapped: {
                            if (dragState.swallowTap) {
                                dragState.swallowTap = false;
                                return;
                            }
                            root.select(tile.position);
                        }
                    }
                }
            }

            // Follows the finger over the whole board: the hold that starts a drag, the drag, and the drop. It only
            // watches, so a tap, a scroll of the page and the buttons behave as they do without it.
            PointHandler {
                id: finger

                objectName: "miniFinger"
                onActiveChanged: {
                    if (active) {
                        dragState.swallowTap = false;
                        const hit = root.tileAt(point.position.x, point.position.y);
                        dragState.pressTile = hit;
                        dragState.pressX = point.position.x;
                        dragState.pressY = point.position.y;
                        dragState.holding = hit !== null && hit.movable;
                        if (dragState.holding)
                            hold.restart();
                    } else {
                        hold.stop();
                        dragState.holding = false;
                        if (dragState.active)
                            root.drop();
                    }
                }
                onPointChanged: {
                    if (!active)
                        return;
                    const x = point.position.x;
                    const y = point.position.y;
                    if (dragState.active) {
                        proxy.x = x - dragState.offsetX;
                        proxy.y = y - dragState.offsetY;
                        const over = root.tileAt(x, y);
                        dragState.target = over && !over.isOther ? over : null;
                    } else if (dragState.holding) {
                        // Moving before the hold is over is a scroll, not a drag.
                        const dx = x - dragState.pressX;
                        const dy = y - dragState.pressY;
                        if (Math.sqrt(dx * dx + dy * dy) > Application.styleHints.startDragDistance) {
                            hold.stop();
                            dragState.holding = false;
                        }
                    }
                }
            }
        }

        // What the selected tile is, and the buttons that do what the drag does.
        Rectangle {
            id: panel

            objectName: "miniPanel"
            width: parent.width
            height: panelColumn.implicitHeight + 2 * Theme.space.rowPadding
            visible: root.selectedTile !== null && root.selectedPosition > 0
            radius: Theme.radius.groupedList
            color: Theme.colour.surface
            Accessible.role: Accessible.Grouping
            Accessible.name: qsTr("Selected tile")
            Accessible.ignored: !visible

            Column {
                id: panelColumn

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: Theme.space.rowPadding
                spacing: Theme.space.sectionGap

                Flow {
                    id: heading

                    width: parent.width
                    spacing: Theme.space.tileGap

                    Rectangle {
                        id: swatch

                        objectName: "miniSwatch"
                        width: Theme.size.badge * 0.75
                        height: Theme.size.badge * 0.75
                        radius: Theme.border.otherWidth * 2.5
                        color: root.selectedTile && !root.selectedTile.isEmpty ? Theme.tileColour(root.selectedTile.colourSlot) : "transparent"
                        border.width: Theme.border.hairline
                        border.color: Theme.colour.border
                        Accessible.ignored: true
                    }

                    Text {
                        id: selectedName

                        objectName: "miniSelectedName"
                        width: Math.min(implicitWidth, heading.width - swatch.width - heading.spacing)
                        text: root.selectedTile ? (root.selectedTile.isEmpty ? qsTr("Empty tile") : root.selectedTile.label) : ""
                        color: Theme.colour.textPrimary
                        font: Theme.font.sheetTitle.font
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: root.selectedTile ? root.selectedTile.accessibleName : ""
                    }

                    Text {
                        id: selectedPlace

                        objectName: "miniSelectedPlace"
                        width: Math.min(implicitWidth, heading.width)
                        text: qsTr("Position %1 of %2").arg(root.selectedPosition).arg(root.positions)
                        color: Theme.colour.textSecondary
                        font: Theme.font.caption.font
                        wrapMode: Text.Wrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }

                Row {
                    id: actions

                    readonly property real buttonWidth: (width - 2 * spacing) / 3

                    width: parent.width
                    spacing: Theme.space.tileGap

                    PanelButton {
                        objectName: "miniMoveEarlier"
                        width: actions.buttonWidth
                        label: qsTr("Move earlier")
                        glyph: "M15 18l-6-6 6-6"
                        enabled: root.selectedTile !== null && root.selectedTile.movable && root.selectedPosition > 1
                        onActivated: root.requestMove(root.selectedPosition, root.selectedPosition - 1)
                    }

                    PanelButton {
                        objectName: "miniMoveLater"
                        width: actions.buttonWidth
                        label: qsTr("Move later")
                        glyph: "M9 6l6 6-6 6"
                        enabled: root.selectedTile !== null && root.selectedTile.movable && root.selectedPosition < root.positions
                        onActivated: root.requestMove(root.selectedPosition, root.selectedPosition + 1)
                    }

                    PanelButton {
                        objectName: "miniReplace"
                        width: actions.buttonWidth
                        label: qsTr("Replace type")
                        glyph: "M7 4l-4 4 4 4M3 8h14M17 20l4-4-4-4M21 16H7"
                        onActivated: root.replaceRequested(root.selectedPosition)
                    }
                }

                Item {
                    objectName: "miniDone"
                    width: parent.width
                    height: Theme.size.minTouchTarget
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Done")
                    Accessible.focusable: true
                    Accessible.onPressAction: root.selectedPosition = 0

                    Text {
                        anchors.centerIn: parent
                        text: qsTr("Done")
                        color: Theme.colour.primaryText
                        font: Theme.font.bodySmall.font
                        Accessible.ignored: true
                    }

                    TapHandler {
                        onTapped: root.selectedPosition = 0
                    }
                }
            }
        }
    }

    // The tile that is being dragged, under the finger. It is on the board, not in the grid, so the grid does not place it.
    Rectangle {
        id: proxy

        objectName: "miniDragProxy"
        visible: dragState.active
        z: 2
        width: root.tileWidth
        height: root.tileHeight
        scale: root.liftScale
        radius: Theme.radius.field
        color: dragState.source ? Theme.tileColour(dragState.source.colourSlot) : Theme.colour.swatch
        border.width: root.outlineWidth
        border.color: Theme.colour.primaryText
        Accessible.ignored: true

        Text {
            anchors.fill: parent
            anchors.margins: Theme.space.tileGap
            text: dragState.source ? dragState.source.label : ""
            color: Theme.colour.tileText
            font.pixelSize: root.labelPixels
            font.weight: Theme.font.tileLabel.weight
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignBottom
            Accessible.ignored: true
        }
    }
}
