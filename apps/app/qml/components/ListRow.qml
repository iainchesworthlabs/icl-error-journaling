// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// One row of a GroupedList: a label on the left, an optional value on the right, and an optional check mark or chevron.
// The row is 48 pt high at least and grows with its text, which wraps and never truncates. A row that is `tappable` is a
// button (it is how a list of choices or a pushed page is made); a row that is not is a label with a value and is read
// as one piece of text.
//
// Inside a GroupedList the first and last rows round their pressed highlight with the group, and the last row has no
// separator.
Item {
    id: root

    required property string text
    property string value: ""
    // A row of a list of choices is `checkable`; the selected one is `checked` and shows a check mark. A row that opens a
    // page shows a chevron.
    property bool checkable: false
    property bool checked: false
    property bool showChevron: false
    // A destructive row ("Retire this error type") has its label in the destructive colour.
    property bool destructive: false
    property bool tappable: true
    property bool showSeparator: true
    property string accessibleName: value.length > 0 ? qsTr("%1, %2").arg(text).arg(value) : text

    readonly property bool pressed: tap.pressed
    readonly property bool isFirst: Positioner.isFirstItem
    readonly property bool isLast: Positioner.isLastItem

    // The row was tapped, or activated by a screen reader or the keyboard.
    signal activated

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    // In a group the row is as wide as the group.
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(Theme.size.listRowHeight, content.implicitHeight + Theme.space.sectionGap)
    activeFocusOnTab: tappable

    Accessible.role: tappable ? Accessible.Button : Accessible.StaticText
    Accessible.name: root.accessibleName
    Accessible.checkable: root.checkable
    Accessible.checked: checked
    Accessible.focusable: tappable
    Accessible.onPressAction: {
        if (root.tappable)
            root.activated();
    }

    Rectangle {
        anchors.fill: parent
        visible: root.pressed
        color: Theme.colour.primaryTint
        topLeftRadius: root.isFirst ? Theme.radius.groupedList : 0
        topRightRadius: root.isFirst ? Theme.radius.groupedList : 0
        bottomLeftRadius: root.isLast ? Theme.radius.groupedList : 0
        bottomRightRadius: root.isLast ? Theme.radius.groupedList : 0
    }

    Row {
        id: content

        x: Theme.space.rowPadding
        y: (root.height - height) / 2
        width: parent.width - 2 * Theme.space.rowPadding
        spacing: Theme.space.sectionGap

        Text {
            id: label

            width: {
                const others = (valueText.visible ? valueText.width : 0) + (markSlot.visible ? markSlot.width : 0);
                const gaps = (valueText.visible ? 1 : 0) + (markSlot.visible ? 1 : 0);
                return Math.max(content.width * 0.4, content.width - others - gaps * content.spacing);
            }
            color: root.destructive ? Theme.colour.destructive : Theme.colour.textPrimary
            font: Theme.font.body.font
            text: root.text
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Text {
            id: valueText

            width: Math.min(valueMetrics.advanceWidth, content.width * 0.5)
            visible: root.value.length > 0
            horizontalAlignment: Text.AlignRight
            color: Theme.colour.textSecondary
            font: Theme.font.body.font
            text: root.value
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Item {
            id: markSlot

            width: Theme.space.screenMargin + Theme.space.tileGap / 2
            height: width
            visible: (root.checkable && root.checked) || root.showChevron

            InputGlyph {
                anchors.fill: parent
                kind: root.checkable && root.checked ? "check" : "chevron"
                colour: root.checkable && root.checked ? Theme.colour.primaryText : Theme.colour.textSecondary
            }
        }
    }

    TextMetrics {
        id: valueMetrics

        font: Theme.font.body.font
        text: root.value
    }

    Rectangle {
        anchors.bottom: parent.bottom
        x: Theme.space.rowPadding
        width: parent.width - Theme.space.rowPadding
        height: Theme.border.hairline
        visible: root.showSeparator && !root.isLast
        color: Theme.colour.divider
    }

    TapHandler {
        id: tap

        enabled: root.tappable

        onTapped: root.activated()
    }

    Keys.onReturnPressed: {
        if (root.tappable)
            root.activated();
    }
    Keys.onSpacePressed: {
        if (root.tappable)
            root.activated();
    }
}
