// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// One row of an editor list: a name with an optional line under it, an optional colour swatch, and the controls that change
// the list. Put it in a GroupedList; the first and last rows round their pressed highlight with the group. The layout is
// the mock-ups': 14 pt of padding, 10 pt between the parts, and a separator that runs the full width of the card.
//
//   the label     one button that opens the row for renaming (`activated`). It is read as "name, detail".
//   the handle    a drag handle (`draggable`) of six dots at the left: holding it and moving it up or down by whole rows
//                 ends in `dragged(rows)`. For a screen reader it is an adjustable control: "increase" is Move later and
//                 "decrease" is Move earlier (the accessibility actions), so a list that shows no arrows (Locations) can
//                 still be reordered without a gesture.
//   the arrows    "Move earlier" and "Move later" (`moveEarlier`, `moveLater`) as 44 pt buttons at the right, shown when
//                 `reorderable` (Error types, where the mock-up draws them); one that cannot move is dimmed and does
//                 nothing.
//   remove        a "Remove" button (`removable`), a cross.
//   chevron       a thin trailing chevron (`chevron`), for a row that opens a page (Note templates).
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
    property bool chevron: false
    // The least height of the row: 56 for a row of a list, 48 for the add row, 64 for a row with two lines of text.
    property real minimumHeight: Theme.size.rowHeightAction
    property string moveEarlierName: qsTr("Move %1 earlier").arg(text)
    property string moveLaterName: qsTr("Move %1 later").arg(text)
    property string removeName: qsTr("Remove %1").arg(text)
    property string reorderName: qsTr("Reorder %1").arg(text)
    property string accessibleName: detail.length > 0 ? qsTr("%1, %2").arg(text).arg(detail) : text

    readonly property bool pressed: tap.pressed
    readonly property bool isFirst: Positioner.isFirstItem
    readonly property bool isLast: Positioner.isLastItem
    readonly property bool dragging: dragHandler.active
    // The gap between the parts of the row.
    readonly property real gap: Theme.space.tileGap + Theme.border.hairline * 2
    // The size of the drawings, and of the swatch.
    readonly property real glyphSize: Theme.size.badge * 5 / 6
    readonly property real swatchSize: Theme.size.badge * 7 / 12

    // The label was tapped, or activated by a screen reader or the keyboard.
    signal activated
    signal moveEarlier
    signal moveLater
    signal removeRequested
    // A drag of the handle ended `rows` rows from where it started: negative is up.
    signal dragged(int rows)

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(minimumHeight, labelColumn.implicitHeight + Theme.space.sectionGap)
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
            objectName: "editorSeparator"
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.border.hairline
            visible: !root.isLast && !root.dragging
            color: Theme.colour.divider
        }

        // The handle, at the left: a 44 pt target around the six dots, which sit 14 pt in from the edge.
        Item {
            id: handle

            objectName: "editorHandle"
            visible: root.draggable
            x: Theme.space.rowPadding + root.glyphSize / 2 - width / 2
            width: visible ? Theme.size.minTouchTarget : 0
            height: parent.height
            Accessible.role: Accessible.Slider
            Accessible.name: root.reorderName
            Accessible.description: qsTr("Drag up or down. Swipe up to move it later and down to move it earlier.")
            Accessible.focusable: true
            Accessible.ignored: !visible
            Accessible.onIncreaseAction: {
                if (root.canMoveLater)
                    root.moveLater();
            }
            Accessible.onDecreaseAction: {
                if (root.canMoveEarlier)
                    root.moveEarlier();
            }

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
            x: root.draggable ? Theme.space.rowPadding + root.glyphSize + root.gap : Theme.space.rowPadding
            y: (parent.height - height) / 2
            width: visible ? root.swatchSize : 0
            height: width
            radius: Theme.radius.field / 3
            color: Theme.tileColour(root.swatchSlot)
            border.width: Theme.border.hairline
            border.color: Theme.colour.border
            Accessible.ignored: true
        }

        // The label: the whole width between the handle or swatch and the controls.
        Item {
            id: labelArea

            // Where the controls at the right begin.
            readonly property real controlsStart: chevronMark.visible ? chevronMark.x : buttons.visible ? buttons.x : root.width - Theme.space.rowPadding

            objectName: "editorLabel"
            x: swatch.visible ? swatch.x + swatch.width + root.gap : (root.draggable || root.action ? Theme.space.rowPadding + root.glyphSize + root.gap : Theme.space.rowPadding)
            width: Math.max(0, controlsStart - x - root.gap)
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

            Column {
                id: labelColumn

                anchors.verticalCenter: parent.verticalCenter
                width: parent.width

                Text {
                    width: parent.width
                    color: root.action ? Theme.colour.primaryText : Theme.colour.textPrimary
                    font: Theme.font.body.font
                    text: root.text
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }

                Item {
                    width: parent.width
                    height: root.detail.length > 0 ? Theme.border.hairline * 2 : 0
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

        // The plus of an add row, at the left.
        EditorGlyph {
            visible: root.action
            x: Theme.space.rowPadding
            y: (parent.height - height) / 2
            kind: "plus"
        }

        // The chevron of a row that opens a page, at the right.
        Icon {
            id: chevronMark

            objectName: "editorChevron"
            visible: root.chevron
            x: root.width - Theme.space.rowPadding - width
            y: (parent.height - height) / 2
            size: Theme.size.badge * 7 / 12
            kind: "chevronRight"
            colour: Theme.colour.borderStrong
        }

        // The buttons, at the right: 44 pt each, 10 pt apart, the last one 14 pt in from the edge.
        Row {
            id: buttons

            visible: root.reorderable || root.removable
            x: parent.width - Theme.space.rowPadding - width
            height: parent.height
            spacing: root.gap

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
