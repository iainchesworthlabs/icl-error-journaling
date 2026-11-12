// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The first-run tip of the Log screen (Stage 10, package 10.6; the mock-up LogFirstRunTip): a card in the information tone
// of Banner, with an "i" icon, a title, three short lines and a Close button. It stands where the Last bar does, so the
// board does not lose a tile, and the Log screen shows it only while the tiles keep their 88 pt minimum beside it.
//
// It is made to be short: the lines are the footnote size with a fixed pitch, so the height of the card is its padding, the
// title and three lines, whatever the font. The Log screen reads `implicitHeight` to know how much room to give it. The
// message is one string with a line break between its lines, so a translator sees the three together.
//
// To a screen reader the card is a message named by its title and described by its lines, with the Close button after it.
// It is not modal and takes no touch outside its own card.
Item {
    id: root

    required property string title
    // The lines, separated by a line break.
    required property string message
    // The accessible name of the Close button.
    property string closeLabel: qsTr("Close")

    // The Close button was activated. The card does not hide itself: the Log screen decides.
    signal closeRequested

    // The padding inside the outline: 8 pt and the outline, so the card stays under 100 pt with three lines.
    readonly property real edge: Theme.border.otherWidth
    readonly property real padding: Theme.space.tileGap
    readonly property real iconSize: Theme.size.badge * 5 / 6
    // The Close button is a 44 pt target that reaches into the padding at the top right corner.
    readonly property real closeReach: (Theme.size.minTouchTarget - Theme.size.badge * 3 / 4) / 2
    readonly property real textLeft: edge + Theme.space.rowPadding + iconSize + Theme.space.tileGap
    readonly property real textRight: edge + Theme.space.rowPadding
    // The title shares its row with the Close button; the lines under it run beneath the button's corner, so they have the room.
    readonly property real closeClearance: Theme.size.minTouchTarget - closeReach

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: card.height

    Rectangle {
        id: card

        objectName: "tipCard"
        width: parent.width
        height: textColumn.height + 2 * (root.padding + root.edge)
        radius: Theme.radius.toast
        color: Theme.colour.primaryTint
        border.width: root.edge
        border.color: Theme.colour.primaryText
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.title
        Accessible.description: root.message

        Icon {
            objectName: "tipIcon"
            x: root.edge + Theme.space.rowPadding
            y: root.edge + root.padding
            size: root.iconSize
            kind: "info"
            colour: Theme.colour.primaryText
        }

        Column {
            id: textColumn

            x: root.textLeft
            y: root.edge + root.padding
            width: card.width - root.textLeft - root.textRight
            spacing: Theme.border.hairline * 2

            Text {
                objectName: "tipTitle"
                width: parent.width - root.closeClearance
                wrapMode: Text.WordWrap
                color: Theme.colour.textPrimary
                font: Theme.font.bannerTitle.font
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.font.bannerTitle.linePitch
                text: root.title
                Accessible.ignored: true
            }

            Text {
                objectName: "tipMessage"
                width: parent.width
                wrapMode: Text.WordWrap
                color: Theme.colour.textPrimary
                font: Theme.font.footnote.font
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.font.footnote.linePitch
                text: root.message
                Accessible.ignored: true
            }
        }

        Item {
            id: closeButton

            objectName: "tipClose"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: root.edge + root.padding - root.closeReach
            anchors.rightMargin: root.edge + Theme.space.rowPadding - root.closeReach
            width: Theme.size.minTouchTarget
            height: Theme.size.minTouchTarget
            Accessible.role: Accessible.Button
            Accessible.name: root.closeLabel
            Accessible.onPressAction: root.closeRequested()

            TapHandler {
                onTapped: root.closeRequested()
            }

            Icon {
                anchors.centerIn: parent
                size: Theme.size.badge * 3 / 4
                kind: "close"
                colour: Theme.colour.textSecondary
            }
        }
    }
}
