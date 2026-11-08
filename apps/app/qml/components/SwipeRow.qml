// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// A row that reveals Copy and Delete when it is swiped to the left (the Review timeline; History uses it with other
// words). Put the row's content in it as children; they are placed inside the row's margins.
//
//     SwipeRow {
//         accessibleName: qsTr("10:20 Mute Delay")
//         onActivated: openEdit(entryId)
//         onCopyRequested: timeline.copyLine(entryId)
//         onDeleteRequested: timeline.deleteEntry(entryId)
//         Text { width: parent.width; text: lineText }
//     }
//
// Accessibility. The row is one button, `activated`, and a screen opens the Edit sheet from it, which also offers Copy and
// Delete. Qt offers QML no custom VoiceOver actions (docs/plan/stage-4.md, U4), so the two actions are also two buttons
// in the accessibility tree, named after the row ("Copy, 10:20 Mute Delay"), that a screen reader reaches without a
// swipe. They sit under the row and are activated where they are; the swipe is the shortcut for a finger.
//
// Layout: the content sits 13 pt from the top and the bottom (Theme.font.rowText, 15 pt with a 1.35 line height, is the
// text of the design; `verticalPadding` sets another), and the row is at least `minimumHeight` (48; Theme.size.rowHeightTall,
// 60, for History). The separator runs the full width of the card; a screen sets `showDivider: false` on the last row of a
// card. The action labels are 14/600.
//
// The row slides with a drag along x. A short drag, or one that is mostly vertical, goes back (a vertical drag scrolls
// the list that holds the row and does not open it). A tap on an open row closes it.
Item {
    id: root

    required property string accessibleName
    property string copyText: qsTr("Copy")
    property string deleteText: qsTr("Delete")
    property bool showCopy: true
    // The surface of the row. Review is black and white, so its screen passes the review surface.
    property color surface: Theme.colour.surface
    // The row is open (its actions are shown). A swipe sets it; a screen sets it to false to close the row.
    property bool revealed: false
    property real minimumHeight: Theme.size.listRowHeight
    // The separator under the row; false for the last row of a card.
    property bool showDivider: true
    // Review passes its own divider colour, as it does its surface.
    property color dividerColour: Theme.colour.divider

    default property alias content: contentItem.data

    readonly property bool pressed: tap.pressed
    // How far the row slides: the width of the actions.
    readonly property real revealWidth: (showCopy ? copyButton.width : 0) + deleteButton.width
    // The padding above and below the content: 13 pt; History sets 8 (its rows are 60 pt high, with 8 pt of padding).
    property real verticalPadding: Theme.space.rowPadding - Theme.border.hairline

    signal activated
    signal copyRequested
    signal deleteRequested

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(minimumHeight, contentItem.childrenRect.height + 2 * verticalPadding)
    clip: true

    // A button of the revealed strip.
    component ActionButton: Item {
        id: action

        required property string label
        required property color fill
        required property color labelColour
        // An inline component cannot see the ids of the file around it, so the row passes what it needs.
        required property string rowName
        required property real rowHeight
        required property real rowWidth
        // The row is open. A closed row has these buttons under its face, where a tap must not reach them; the
        // accessibility press action below is not gated, so a screen reader can still press them.
        required property bool armed
        signal triggered

        // 76 pt, wider for a long word, and never more than two fifths of the row.
        width: Math.min(rowWidth * 0.4, Math.max(Theme.size.minTouchTarget + 2 * Theme.space.screenMargin, metrics.advanceWidth + 2 * Theme.space.rowPadding))
        height: rowHeight
        activeFocusOnTab: true

        Accessible.role: Accessible.Button
        Accessible.name: qsTr("%1, %2").arg(action.label).arg(action.rowName)
        Accessible.focusable: true
        Accessible.onPressAction: action.triggered()

        TextMetrics {
            id: metrics

            font: Theme.font.actionLabel.font
            text: action.label
        }
        Rectangle {
            anchors.fill: parent
            color: action.fill
        }
        Text {
            id: labelText

            anchors.fill: parent
            anchors.margins: Theme.space.tileGap / 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            color: action.labelColour
            font: Theme.font.actionLabel.font
            text: action.label
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }
        TapHandler {
            enabled: action.armed

            onTapped: action.triggered()
        }
        Keys.onReturnPressed: action.triggered()
        Keys.onSpacePressed: action.triggered()
    }

    // The row itself.
    Rectangle {
        id: face

        objectName: "face"
        width: parent.width
        height: parent.height
        color: root.pressed ? Theme.colour.primaryTint : root.surface

        Accessible.role: Accessible.Button
        Accessible.name: root.accessibleName
        Accessible.focusable: true
        Accessible.onPressAction: root.activated()

        Item {
            id: contentItem

            x: Theme.space.rowPadding
            y: Math.max(0, (face.height - childrenRect.height) / 2)
            width: parent.width - 2 * Theme.space.rowPadding
            height: childrenRect.height
        }

        Rectangle {
            objectName: "swipeDivider"
            anchors.bottom: parent.bottom
            visible: root.showDivider
            width: parent.width
            height: Theme.border.hairline
            color: root.dividerColour
        }

        TapHandler {
            id: tap

            onTapped: {
                if (root.revealed)
                    root.revealed = false;
                else
                    root.activated();
            }
        }

        DragHandler {
            id: drag

            target: face
            xAxis.minimum: -root.revealWidth
            xAxis.maximum: 0
            yAxis.enabled: false

            onActiveChanged: {
                if (active)
                    return;
                const open = face.x < -root.revealWidth / 2;
                root.revealed = open;
                root.slideTo(open ? -root.revealWidth : 0);
            }
        }

        activeFocusOnTab: true
        Keys.onReturnPressed: root.activated()
        Keys.onSpacePressed: root.activated()
    }

    // The actions, at the right edge.
    Row {
        id: actions

        anchors.right: parent.right
        height: parent.height
        // Under the row, though after it in the tree: a screen reader reads the row first, then Copy and Delete.
        z: -1

        ActionButton {
            id: copyButton

            objectName: "copyAction"
            visible: root.showCopy
            label: root.copyText
            fill: Theme.colour.primary
            labelColour: Theme.colour.textOnPrimary
            rowName: root.accessibleName
            rowHeight: root.height
            rowWidth: root.width
            armed: root.revealed
            onTriggered: {
                root.copyRequested();
                root.revealed = false;
            }
        }
        ActionButton {
            id: deleteButton

            objectName: "deleteAction"
            label: root.deleteText
            fill: Theme.colour.destructiveFill
            labelColour: Theme.colour.textOnDestructive
            rowName: root.accessibleName
            rowHeight: root.height
            rowWidth: root.width
            armed: root.revealed
            onTriggered: {
                root.deleteRequested();
                root.revealed = false;
            }
        }
    }

    XAnimator {
        id: slide

        target: face
        duration: Theme.motion.swipeReleaseMs
        easing.type: Theme.motion.easingSpring
    }

    // Slide to the open or the closed place; with Reduce Motion the row simply jumps.
    function slideTo(place: real): void {
        slide.stop();
        if (Theme.reduceMotion || face.x === place) {
            face.x = place;
            return;
        }
        slide.to = place;
        slide.restart();
    }

    onRevealedChanged: slideTo(revealed ? -revealWidth : 0)
}
