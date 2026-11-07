// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// One row of an editor list: a name with an optional line under it, an optional colour swatch, and the buttons that
// change the list. Put it in a GroupedList; the first and last rows round their pressed highlight with the group.
//
//   the label     one button that opens the row for renaming (`activated`). It is read as "name, detail".
//   the arrows    "Move earlier" and "Move later" (`moveEarlier`, `moveLater`): the way to reorder that needs no drag,
//                 and the one a screen reader uses. Shown when `reorderable`; one that cannot move is dimmed and does
//                 nothing.
//   the handle    a drag handle (`draggable`): holding it and moving it up or down by whole rows ends in `dragged(rows)`.
//   remove        a "Remove" button (`removable`).
//
// An `action` row is the "Add ..." row at the head of a list: the plus and the label in the link colour.
//
// The row decides nothing: a screen turns the signals into calls on the view model.
Item {
    id: root

    required property string text
    property string detail: ""
    // The colour slot of the error type the swatch shows (1 to 11); 0 hides the swatch.
    property int swatchSlot: 0
    property bool action: false
    property bool reorderable: false
    property bool canMoveEarlier: true
    property bool canMoveLater: true
    property bool draggable: false
    property bool removable: false
    property string moveEarlierName: qsTr("Move %1 earlier").arg(text)
    property string moveLaterName: qsTr("Move %1 later").arg(text)
    property string removeName: qsTr("Remove %1").arg(text)
    property string accessibleName: detail.length > 0 ? qsTr("%1, %2").arg(text).arg(detail) : text

    readonly property bool pressed: tap.pressed
    readonly property bool isFirst: Positioner.isFirstItem
    readonly property bool isLast: Positioner.isLastItem
    readonly property bool dragging: dragHandler.active

    // The label was tapped, or activated by a screen reader or the keyboard.
    signal activated
    signal moveEarlier
    signal moveLater
    signal removeRequested
    // A drag of the handle ended `rows` rows from where it started: negative is up.
    signal dragged(int rows)

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Theme.size.listRowHeight + Theme.space.tileGap, labelColumn.implicitHeight + Theme.space.sectionGap)
    z: dragHandler.active ? 1 : 0

    Item {
        id: surface

        width: parent.width
        height: parent.height
        y: dragHandler.active ? dragHandler.centroid.scenePosition.y - dragHandler.centroid.scenePressPosition.y : 0

        Rectangle {
            anchors.fill: parent
            visible: root.pressed || root.dragging
            color: Theme.colour.primaryTint
            topLeftRadius: root.isFirst ? Theme.radius.groupedList : 0
            topRightRadius: root.isFirst ? Theme.radius.groupedList : 0
            bottomLeftRadius: root.isLast ? Theme.radius.groupedList : 0
            bottomRightRadius: root.isLast ? Theme.radius.groupedList : 0
        }

        Rectangle {
            anchors.bottom: parent.bottom
            x: Theme.space.rowPadding
            width: parent.width - Theme.space.rowPadding
            height: Theme.border.hairline
            visible: !root.isLast && !root.dragging
            color: Theme.colour.divider
        }

        // The handle, at the left.
        Item {
            id: handle

            objectName: "editorHandle"
            visible: root.draggable
            x: Theme.space.tileGap / 2
            width: visible ? Theme.size.minTouchTarget : 0
            height: parent.height
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Reorder %1").arg(root.text)
            Accessible.description: qsTr("Drag up or down, or use Move earlier and Move later.")
            Accessible.focusable: true

            EditorGlyph {
                anchors.centerIn: parent
                kind: "handle"
                colour: Theme.colour.textSecondary
            }

            DragHandler {
                id: dragHandler

                target: null
                enabled: root.draggable
                xAxis.enabled: false
                onActiveChanged: {
                    if (!active)
                        root.dragged(Math.round((centroid.scenePosition.y - centroid.scenePressPosition.y) / root.height));
                }
            }
        }

        Rectangle {
            id: swatch

            objectName: "editorSwatch"
            visible: root.swatchSlot > 0
            x: root.draggable ? handle.x + handle.width : Theme.space.rowPadding
            y: (parent.height - height) / 2
            width: visible ? Theme.size.badge * 0.6 : 0
            height: width
            radius: Theme.radius.field / 3
            color: Theme.tileColour(root.swatchSlot)
            border.width: Theme.border.hairline
            border.color: Theme.colour.border
            Accessible.ignored: true
        }

        // The label: the whole width between the handle and the buttons.
        Item {
            id: labelArea

            objectName: "editorLabel"
            x: swatch.visible ? swatch.x + swatch.width + Theme.space.sectionGap : (root.draggable ? handle.x + handle.width : Theme.space.rowPadding)
            width: Math.max(0, buttons.x - x - Theme.space.tileGap)
            height: parent.height
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: root.accessibleName
            Accessible.focusable: true
            Accessible.onPressAction: root.activated()

            Keys.onReturnPressed: root.activated()
            Keys.onSpacePressed: root.activated()

            TapHandler {
                id: tap

                onTapped: root.activated()
            }

            Row {
                anchors.verticalCenter: parent.verticalCenter
                width: parent.width
                spacing: Theme.space.sectionGap

                EditorGlyph {
                    visible: root.action
                    anchors.verticalCenter: parent.verticalCenter
                    kind: "plus"
                }

                Column {
                    id: labelColumn

                    width: parent.width - (root.action ? Theme.size.badge + Theme.space.sectionGap : 0)

                    Text {
                        width: parent.width
                        color: root.action ? Theme.colour.primaryText : Theme.colour.textPrimary
                        font: Theme.font.body.font
                        text: root.text
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    Text {
                        width: parent.width
                        visible: root.detail.length > 0
                        color: Theme.colour.textSecondary
                        font: Theme.font.caption.font
                        text: root.detail
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                }
            }
        }

        // The buttons, at the right.
        Row {
            id: buttons

            x: parent.width - width - Theme.space.tileGap / 2
            height: parent.height

            RowButton {
                objectName: "editorMoveEarlier"
                visible: root.reorderable
                enabled: root.canMoveEarlier
                glyph: "up"
                name: root.moveEarlierName
                onTriggered: root.moveEarlier()
            }

            RowButton {
                objectName: "editorMoveLater"
                visible: root.reorderable
                enabled: root.canMoveLater
                glyph: "down"
                name: root.moveLaterName
                onTriggered: root.moveLater()
            }

            RowButton {
                objectName: "editorRemove"
                visible: root.removable
                glyph: "remove"
                glyphColour: Theme.colour.destructive
                name: root.removeName
                onTriggered: root.removeRequested()
            }
        }
    }

    // A 44 pt button of the right side of the row.
    component RowButton: Item {
        id: button

        required property string glyph
        required property string name
        property color glyphColour: Theme.colour.primaryText

        signal triggered

        width: Theme.size.minTouchTarget
        height: Theme.size.minTouchTarget
        y: parent ? (parent.height - height) / 2 : 0
        opacity: enabled ? 1 : 0.4
        activeFocusOnTab: true
        Accessible.role: Accessible.Button
        Accessible.name: button.name
        Accessible.focusable: true
        Accessible.onPressAction: {
            if (button.enabled)
                button.triggered();
        }
        Keys.onReturnPressed: button.triggered()
        Keys.onSpacePressed: button.triggered()

        EditorGlyph {
            anchors.centerIn: parent
            kind: button.glyph
            colour: button.glyphColour
        }

        TapHandler {
            enabled: button.enabled
            onTapped: button.triggered()
        }
    }
}
