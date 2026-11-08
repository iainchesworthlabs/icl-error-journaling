// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// One of the plain choices of the software version sheet ("It changed during the day", "I typed it wrong"): a title, a
// line of explanation and a round mark at the left. The selected card has a 2 pt blue border and a filled mark with a check
// in it; an unselected one has a hairline border and an empty ring, so the state is never colour alone.
//
// The card does not select itself. It emits `clicked` and the screen sets `selected` on exactly one of its cards, which
// keeps "exactly one" in one place. It is a radio button to a screen reader.
//
// Layout (ChangeVersion): 14 pt padding inside the border, a 22 pt mark, the text 12 pt after it (66 pt from the screen
// edge), the title in 16/600 and the description in 14 pt with a 1.35 line height, 4 pt under it. The fill is
// `surface`, surfaceRaised by default, because the cards sit on a sheet.
Item {
    id: root

    required property string title
    property string description: ""
    property bool selected: false
    property string accessibleName: title
    property color surface: Theme.colour.surfaceRaised

    readonly property bool pressed: tap.pressed
    // The inside of the 2 pt border of a selected card; the text does not move when the selection does.
    readonly property real inset: Theme.border.hairline * 2 + Theme.space.rowPadding

    signal clicked

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: Math.max(Theme.size.minTouchTarget, textColumn.height + 2 * inset)
    width: parent ? parent.width : implicitWidth
    activeFocusOnTab: true

    Accessible.role: Accessible.RadioButton
    Accessible.name: root.accessibleName
    Accessible.description: root.description
    Accessible.checkable: true
    Accessible.checked: root.selected
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.groupedList
        color: root.pressed ? Theme.colour.primaryTint : root.surface
        border.width: root.selected || root.activeFocus ? Theme.border.hairline * 2 : Theme.border.hairline
        border.color: root.selected || root.activeFocus ? Theme.colour.primaryText : Theme.colour.border
    }

    // The mark: a ring, or a filled disc with a check.
    Rectangle {
        id: mark

        objectName: "choiceMark"
        x: root.inset
        y: root.inset + Theme.border.hairline
        width: Theme.space.screenMargin + Theme.space.tileGap * 0.75
        height: width
        radius: width / 2
        color: root.selected ? Theme.colour.primary : "transparent"
        border.width: root.selected ? 0 : Theme.border.hairline * 2
        border.color: Theme.colour.borderStrong

        Icon {
            anchors.centerIn: parent
            size: Theme.size.badge * 7 / 12
            strokeWidth: Theme.border.otherWidth * 2
            visible: root.selected
            kind: "check"
            colour: Theme.colour.textOnPrimary
        }
    }

    Column {
        id: textColumn

        objectName: "choiceText"
        x: mark.x + mark.width + Theme.space.sectionGap
        y: root.inset
        width: parent.width - x - root.inset
        spacing: Theme.space.tileGap / 2

        Text {
            width: parent.width
            color: Theme.colour.textPrimary
            font: Theme.font.body.selectedFont
            text: root.title
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
        Text {
            width: parent.width
            visible: root.description.length > 0
            color: Theme.colour.textSecondary
            font: Theme.font.description.font
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.font.description.linePitch
            text: root.description
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    TapHandler {
        id: tap

        onTapped: root.clicked()
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
