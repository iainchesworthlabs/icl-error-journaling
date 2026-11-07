// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// A row of a GroupedList with an on/off switch at its right (Stage 4, package 4.12): "Include a totals line". Like the
// choice card, the row does not change itself. It emits `toggled(wanted)` and the screen asks the model; the switch moves
// when `checked` does, which is when the setting has been saved. The state is shown by the side the knob is on as well as
// by the colour of the track, and a screen reader hears it as a checked box.
//
// This file is a part of SettingsScreen and is tested through it.
Item {
    id: root

    required property string text
    property string description: ""
    property bool checked: false
    property string accessibleName: text

    readonly property bool pressed: tap.pressed
    readonly property bool isLast: Positioner.isLastItem

    // The row was tapped or activated: `wanted` is the state it asks for.
    signal toggled(bool wanted)

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Theme.size.listRowHeight, labels.height + Theme.space.sectionGap)
    activeFocusOnTab: true

    Accessible.role: Accessible.CheckBox
    Accessible.name: root.accessibleName
    Accessible.description: root.description
    Accessible.checkable: true
    Accessible.checked: root.checked
    Accessible.focusable: true
    Accessible.onPressAction: root.toggled(!root.checked)

    Rectangle {
        anchors.fill: parent
        visible: root.pressed
        color: Theme.colour.primaryTint
        bottomLeftRadius: root.isLast ? Theme.radius.groupedList : 0
        bottomRightRadius: root.isLast ? Theme.radius.groupedList : 0
    }

    Column {
        id: labels

        x: Theme.space.rowPadding
        y: (root.height - height) / 2
        width: root.width - 3 * Theme.space.rowPadding - track.width

        Text {
            width: parent.width
            color: Theme.colour.textPrimary
            font: Theme.font.body.font
            text: root.text
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Text {
            width: parent.width
            visible: root.description.length > 0
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: root.description
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    Rectangle {
        id: track

        objectName: "settingsSwitchTrack"
        x: root.width - Theme.space.rowPadding - width
        y: (root.height - height) / 2
        width: Theme.size.minTouchTarget + Theme.space.tileGap
        height: Theme.size.chipHeight
        radius: height / 2
        color: root.checked ? Theme.colour.primary : Theme.colour.searchBackground
        border.width: root.checked ? 0 : Theme.border.hairline * 2
        border.color: Theme.colour.borderStrong
        Accessible.ignored: true

        Rectangle {
            objectName: "settingsSwitchKnob"
            x: root.checked ? parent.width - width - Theme.space.tileGap / 2 : Theme.space.tileGap / 2
            y: (parent.height - height) / 2
            width: Theme.size.badge
            height: width
            radius: width / 2
            color: root.checked ? Theme.colour.textOnPrimary : Theme.colour.borderStrong
            Accessible.ignored: true
        }
    }

    Rectangle {
        anchors.bottom: parent.bottom
        x: Theme.space.rowPadding
        width: parent.width - Theme.space.rowPadding
        height: Theme.border.hairline
        visible: !root.isLast
        color: Theme.colour.divider
    }

    TapHandler {
        id: tap

        onTapped: root.toggled(!root.checked)
    }

    Keys.onReturnPressed: root.toggled(!root.checked)
    Keys.onSpacePressed: root.toggled(!root.checked)
}
