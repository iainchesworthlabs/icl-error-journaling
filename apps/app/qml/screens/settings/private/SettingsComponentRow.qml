// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// One open-source component in the About list (PRF-12): its name and version on the first line, its licence under it, and a
// chevron, like a ListRow that holds two lines. The row grows with its text, which wraps and never truncates. A screen reader
// reads it as one button, "Name version, licence".
//
// ListRow has one line of label and one of value, and a licence expression such as "LGPL-3.0-only OR GPL-3.0-only" does not fit
// beside a name, so the licence has its own line here. The look is ListRow's: the same padding, pressed tint, chevron and
// hairline.
//
// This file is a part of SettingsAboutPage and is tested through SettingsScreen.
Item {
    id: root

    required property string name
    property string version: ""
    property string licence: ""

    readonly property bool isFirst: Positioner.isFirstItem
    readonly property bool isLast: Positioner.isLastItem
    readonly property string title: version.length > 0 ? qsTr("%1 %2").arg(name).arg(version) : name
    readonly property bool pressed: tap.pressed

    signal activated

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Theme.size.listRowHeight, column.implicitHeight + Theme.space.sectionGap)
    activeFocusOnTab: true

    Accessible.role: Accessible.Button
    Accessible.name: licence.length > 0 ? qsTr("%1, %2").arg(title).arg(licence) : title
    Accessible.focusable: true
    Accessible.onPressAction: root.activated()

    Rectangle {
        anchors.fill: parent
        visible: root.pressed
        color: Theme.colour.primaryTint
        topLeftRadius: root.isFirst ? Theme.radius.groupedList : 0
        topRightRadius: root.isFirst ? Theme.radius.groupedList : 0
        bottomLeftRadius: root.isLast ? Theme.radius.groupedList : 0
        bottomRightRadius: root.isLast ? Theme.radius.groupedList : 0
    }

    Column {
        id: column

        x: Theme.space.rowPadding
        y: (root.height - height) / 2
        width: mark.x - Theme.space.tileGap - x
        spacing: Theme.border.hairline * 2

        Text {
            objectName: "componentName"
            width: parent.width
            color: Theme.colour.textPrimary
            font: Theme.font.body.font
            text: root.title
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Text {
            objectName: "componentLicence"
            width: parent.width
            visible: root.licence.length > 0
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: root.licence
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    Icon {
        id: mark

        x: root.width - Theme.space.rowPadding - width
        y: (root.height - height) / 2
        size: Theme.size.badge * 7 / 12
        kind: "chevronRight"
        colour: Theme.colour.borderStrong
    }

    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: Theme.border.hairline
        visible: !root.isLast
        color: Theme.colour.divider
    }

    TapHandler {
        id: tap

        onTapped: root.activated()
    }

    Keys.onReturnPressed: root.activated()
    Keys.onSpacePressed: root.activated()
}
