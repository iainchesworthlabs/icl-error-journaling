// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The filled button of the design: 54 pt high, 14 pt radius, the deep blue fill and a white label (the "Start logging"
// and "Log the change" buttons). `compact` is the 44 pt form that sits beside a field (the composer's "Add").
//
// It is a plain item with a tap handler and not a Basic control, because it supplies no behaviour a control would: the
// press state is the handler's, the activation is the `clicked` signal, and the accessibility press action and the
// Return and Space keys call the same signal, so every way to activate it is one path. A disabled button is dimmed, is
// reported as disabled and does not react.
Item {
    id: root

    required property string text
    property bool compact: false
    // What a screen reader says; the label by default.
    property string accessibleName: text

    readonly property bool pressed: tap.pressed

    signal clicked

    implicitWidth: Math.max(label.implicitWidth + 2 * Theme.space.screenMargin, Theme.size.minTouchTarget * 1.5)
    implicitHeight: Math.max(compact ? Theme.size.fieldHeight : Theme.size.primaryButtonHeight, label.implicitHeight + 2 * Theme.space.sectionGap)
    activeFocusOnTab: true
    opacity: enabled ? 1 : 0.4

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: root.compact ? Theme.radius.field : Theme.radius.primaryButton
        color: root.pressed ? Theme.colour.primaryPressed : Theme.colour.primary
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
        color: Theme.colour.textOnPrimary
        font: root.compact ? Theme.font.bodySmall.font : Theme.font.sheetTitle.font
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
