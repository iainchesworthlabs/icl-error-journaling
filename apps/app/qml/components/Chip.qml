// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// A template chip: the 32 pt pill with a hairline border that starts a note ("Played song"). It is drawn 32 pt high and
// its touch area is 44 pt, through the margin of its tap handler. The name never truncates: a name wider than `maxWidth`
// wraps onto more lines and the chip grows.
Item {
    id: root

    required property string text
    // The widest the chip may be; 0 = as wide as the name needs.
    property real maxWidth: 0
    property string accessibleName: text

    readonly property bool pressed: tap.pressed

    signal clicked

    implicitWidth: {
        const wanted = label.implicitWidthUnwrapped + 2 * Theme.space.sectionGap;
        return maxWidth > 0 ? Math.min(wanted, maxWidth) : wanted;
    }
    implicitHeight: Math.max(Theme.size.chipHeight, label.implicitHeight + Theme.space.tileGap * 2)
    width: implicitWidth
    height: implicitHeight
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: Math.min(Theme.radius.chip, height / 2)
        color: root.pressed ? Theme.colour.primaryTint : Theme.colour.fieldBackground
        border.width: root.activeFocus ? Theme.border.otherWidth : Theme.border.hairline
        border.color: root.activeFocus ? Theme.colour.primaryText : Theme.colour.border
    }

    Text {
        id: label

        // The width of the name on one line, which the chip asks for before it is limited.
        readonly property real implicitWidthUnwrapped: metrics.advanceWidth

        anchors.fill: parent
        anchors.leftMargin: Theme.space.sectionGap
        anchors.rightMargin: Theme.space.sectionGap
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: Theme.colour.textPrimary
        font: Theme.font.caption.font
        text: root.text
        wrapMode: Text.WordWrap
        Accessible.ignored: true

        TextMetrics {
            id: metrics

            font: label.font
            text: label.text
        }
    }

    TapHandler {
        id: tap

        // Up to the 44 pt touch target all round.
        margin: Math.max(0, (Theme.size.minTouchTarget - Math.min(root.width, root.height)) / 2)

        onTapped: root.clicked()
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
