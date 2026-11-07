// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The plain text button under a filled one ("Cancel" under "Replace my setup"): 54 pt high, no fill, the link colour.
// A tap, the accessibility press action and the Return and Space keys all emit `clicked`.
//
// This file is a part of the import pages and is tested through them.
Item {
    id: root

    required property string text
    property string accessibleName: text

    readonly property bool pressed: tap.pressed

    signal clicked

    implicitWidth: Math.max(label.implicitWidth + 2 * Theme.space.screenMargin, Theme.size.minTouchTarget * 1.5)
    implicitHeight: Math.max(Theme.size.primaryButtonHeight, label.implicitHeight + 2 * Theme.space.sectionGap)
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.primaryButton
        color: root.pressed ? Theme.colour.primaryTint : "transparent"
        border.width: root.activeFocus ? Theme.border.otherWidth : 0
        border.color: Theme.colour.primaryText
    }

    Text {
        id: label

        anchors.fill: parent
        anchors.leftMargin: Theme.space.rowPadding
        anchors.rightMargin: Theme.space.rowPadding
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        color: Theme.colour.primaryText
        font: Theme.font.sheetTitle.font
        text: root.text
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    TapHandler {
        id: tap

        onTapped: root.clicked()
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
