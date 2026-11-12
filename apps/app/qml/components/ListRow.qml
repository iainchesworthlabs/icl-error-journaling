// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// One row of a GroupedList: an optional leading icon, a label, an optional value on the right, and an optional check mark
// or chevron. The row grows with its text, which wraps and never truncates. A row that is `tappable` is a button (it is
// how a list of choices or a pushed page is made); a row that is not is a label with a value and is read as one piece of
// text.
//
// Options:
//   minimumHeight      48 by default; Theme.size.rowHeightCompact (44) for the settings rows, rowHeightForm (52) for
//                      Edit entry and the import errors, rowHeightAction (56) for the new-log row
//   leadingIcon        an Icon kind drawn 20 pt before the label, 10 pt from it ("plus" for the new-log row)
//   leadingIconColour  primaryText by default; leadingIconStroke is its stroke on the 24 grid (2.2 for the plus)
//   labelColour        textPrimary by default (destructive when `destructive`); primaryText for an action row
//   labelFont          body (16) by default; Theme.font.rowText.font (15) for the rows of the Other picker's board group
//   valueFont          body (16) by default; Theme.font.caption.font (13) for a count
//   valueInset         extra room right of the value (8 for the counts of ImportConfirm)
//   valueSwatch        a colour for a 12 pt swatch (3 pt radius) 10 pt before the value: the type's tile colour on
//                      Edit entry's Error row. Transparent (the default) for none.
//   valueChip          the value as a time chip: 34 pt high, 12 pt side padding, an 8 pt radius, timeChipBackground,
//                      16/500 tabular figures in textPrimary (the Time row of Edit entry and the version sheet)
//   fullWidthSeparator the separator runs the full width of the card, as every mock-up has it (the default); false
//                      insets it by the row padding
// The chevron is the thin one of the mock-ups: 14 pt, stroke 2 on the 24 grid, borderStrong, 8 pt from the value.
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
    // A row that opens an address in the browser shows the arrow leaving a box instead of the chevron (Stage 10, 10.6).
    property bool showExternal: false
    // A destructive row ("Retire this error type") has its label in the destructive colour.
    property bool destructive: false
    property bool tappable: true
    property bool showSeparator: true
    property bool fullWidthSeparator: true
    property real minimumHeight: Theme.size.listRowHeight
    property string leadingIcon: ""
    property color leadingIconColour: Theme.colour.primaryText
    property real leadingIconStroke: Theme.border.hairline * 2
    property color labelColour: destructive ? Theme.colour.destructive : Theme.colour.textPrimary
    property font labelFont: Theme.font.body.font
    property font valueFont: Theme.font.body.font
    property real valueInset: 0
    property color valueSwatch: "transparent"
    property bool valueChip: false

    readonly property bool hasValue: value.length > 0
    property string accessibleName: value.length > 0 ? qsTr("%1, %2").arg(text).arg(value) : text

    readonly property bool pressed: tap.pressed
    readonly property bool isFirst: Positioner.isFirstItem
    readonly property bool isLast: Positioner.isLastItem
    // The gap between the leading icon, the label and the value, and the gap before the chevron.
    readonly property real gap: Theme.space.tileGap + Theme.border.hairline * 2
    readonly property real markGap: Theme.space.tileGap
    readonly property real contentWidth: width - 2 * Theme.space.rowPadding

    // The row was tapped, or activated by a screen reader or the keyboard.
    signal activated

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    // In a group the row is as wide as the group.
    width: parent ? parent.width : implicitWidth
    implicitHeight: Math.max(minimumHeight, Math.max(label.implicitHeight, root.hasValue ? valueBox.height : 0) + Theme.space.sectionGap)
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

    Icon {
        id: leading

        objectName: "rowLeadingIcon"
        visible: root.leadingIcon.length > 0
        x: Theme.space.rowPadding
        y: (root.height - height) / 2
        size: Theme.size.badge * 5 / 6
        kind: root.leadingIcon
        colour: root.leadingIconColour
        strokeWidth: root.leadingIconStroke
    }

    Text {
        id: label

        objectName: "rowLabel"
        x: leading.visible ? leading.x + leading.width + root.gap : Theme.space.rowPadding
        y: (root.height - height) / 2
        width: {
            const start = swatch.visible ? swatch.x : valueBox.x;
            const end = root.hasValue ? start - root.gap : markSlot.visible ? markSlot.x - root.gap : root.width - Theme.space.rowPadding;
            return Math.max(root.contentWidth * 0.4, end - x);
        }
        color: root.labelColour
        font: root.labelFont
        text: root.text
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    // The swatch before the value.
    Rectangle {
        id: swatch

        objectName: "rowSwatch"
        visible: root.hasValue && root.valueSwatch.a > 0
        x: valueBox.x - root.gap - width
        y: (root.height - height) / 2
        width: Theme.size.badge / 2
        height: width
        radius: Theme.border.otherWidth * 2
        color: root.valueSwatch
    }

    // The value, plain or on a time chip.
    Rectangle {
        id: valueBox

        objectName: "rowValueBox"
        readonly property real padding: root.valueChip ? Theme.space.sectionGap : 0

        visible: root.hasValue
        x: (markSlot.visible ? markSlot.x - root.markGap : root.width - Theme.space.rowPadding) - root.valueInset - width
        y: (root.height - height) / 2
        width: valueText.width + 2 * padding
        height: root.valueChip ? Math.max(Theme.size.chipHeight + Theme.border.hairline * 2, valueText.implicitHeight) : valueText.implicitHeight
        radius: Theme.radius.timeChip
        color: root.valueChip ? Theme.colour.timeChipBackground : "transparent"

        Text {
            id: valueText

            objectName: "rowValue"
            x: valueBox.padding
            y: (valueBox.height - height) / 2
            width: Math.min(Math.ceil(valueMetrics.advanceWidth) + Theme.border.hairline, root.contentWidth * 0.5)
            horizontalAlignment: Text.AlignRight
            color: root.valueChip ? Theme.colour.textPrimary : Theme.colour.textSecondary
            font: root.valueChip ? Theme.font.timeChip.font : root.valueFont
            text: root.value
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    // The check mark (20 pt) or the chevron (14 pt), at the right edge.
    Icon {
        id: markSlot

        objectName: "rowMark"
        readonly property bool check: root.checkable && root.checked

        visible: check || root.showChevron || root.showExternal
        x: root.width - Theme.space.rowPadding - width
        y: (root.height - height) / 2
        size: check ? Theme.size.badge * 5 / 6 : root.showExternal ? Theme.size.badge * 3 / 4 : Theme.size.badge * 7 / 12
        kind: check ? "check" : root.showExternal ? "external" : "chevronRight"
        colour: check ? Theme.colour.primaryText : root.showExternal ? Theme.colour.textSecondary : Theme.colour.borderStrong
    }

    TextMetrics {
        id: valueMetrics

        font: root.valueChip ? Theme.font.timeChip.font : root.valueFont
        text: root.value
    }

    Rectangle {
        objectName: "rowSeparator"
        anchors.bottom: parent.bottom
        x: root.fullWidthSeparator ? 0 : Theme.space.rowPadding
        width: parent.width - x
        height: Theme.border.hairline
        visible: root.showSeparator && !root.isLast
        color: Theme.colour.divider
    }

    TapHandler {
        id: tap

        enabled: root.tappable

        onTapped: {
            // A tap on a row ends the typing in a field beside it: the row takes the focus, the field saves what is typed
            // and the keyboard goes, before the row opens a sheet or a page. A sheet then gives the focus back to the row
            // and not to the field.
            root.forceActiveFocus(Qt.MouseFocusReason);
            root.activated();
        }
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
