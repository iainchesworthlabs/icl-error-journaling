// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The "Last" bar of the Log screen (Stage 4, package 4.5): the latest line of the log, how many entries there are today,
// and a chevron. A tap opens Review. The line is the one TimelineModel offers as `lastLine`, in the export wording, and the
// bar never builds one itself.
//
//   lastLine     the latest line, or empty when the log has no entry (timeline.lastLine)
//   entryCount   the entries of today (timeline.entryCount)
//   skeleton     draw the shape only, in a neutral colour, with no text (the launch screen)
//
//   tapped()     the bar was tapped: the screen shows Review
//
// At the default size the caption, the line and the count are on one row, as in the design. When the text is too large for
// that, the line moves under the caption and the count, so that the count is never cut off. The line is on one row and is
// cut with an ellipsis when it is long, as the design has it (the board below must not move); the accessible name always
// has the whole line. The bar is 44 pt high at least and grows with the text size.
//
// Look (Main): 14 pt padding, 10 pt gaps, the line in 14 pt, the 14 pt chevron Icon in textSecondary, the card shadow.
Item {
    id: root

    required property string lastLine
    required property int entryCount
    property bool skeleton: false

    signal tapped

    readonly property bool hasEntries: lastLine.length > 0
    readonly property real chevronSize: Theme.size.badge * 7 / 12
    readonly property real gap: Theme.space.tileGap + Theme.border.hairline * 2
    readonly property real contentWidth: width - 2 * Theme.space.rowPadding
    // The least room the line is given beside the caption and the count: about a tile.
    readonly property real lineMinimum: Theme.size.tileWidth
    readonly property real countWidth: count.visible ? count.implicitWidth + gap : 0
    readonly property bool stacked: caption.implicitWidth + countWidth + chevronSize + 2 * gap + lineMinimum > contentWidth
    readonly property real firstRowHeight: Math.max(caption.implicitHeight, count.visible ? count.height : 0)

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: Math.max(Theme.size.minTouchTarget, content.implicitHeight + Theme.space.tileGap)

    Accessible.role: root.skeleton ? Accessible.NoRole : Accessible.Button
    Accessible.name: root.hasEntries ? qsTr("Last entry: %1, %n today", "", root.entryCount).arg(root.lastLine) : qsTr("Last entry: none yet")
    Accessible.description: qsTr("Opens Review")
    Accessible.ignored: root.skeleton
    Accessible.focusable: !root.skeleton
    Accessible.onPressAction: {
        if (!root.skeleton)
            root.tapped();
    }

    Rectangle {
        objectName: "lastBarShadow"
        visible: !root.skeleton
        width: parent.width
        height: parent.height
        y: Theme.shadow.card.offsetY
        radius: Theme.radius.groupedList
        color: Theme.shadow.card.colour
    }

    Rectangle {
        objectName: "lastBarBackground"
        anchors.fill: parent
        radius: Theme.radius.groupedList
        color: root.skeleton ? Theme.colour.swatch : (tap.pressed ? Theme.colour.primaryTint : Theme.colour.surface)
        Accessible.ignored: true
    }

    Item {
        id: content

        objectName: "lastBarContent"
        x: Theme.space.rowPadding
        width: root.contentWidth
        implicitHeight: root.stacked ? root.firstRowHeight + root.gap / 2 + line.implicitHeight : Math.max(root.firstRowHeight, line.implicitHeight)
        height: implicitHeight
        anchors.verticalCenter: parent.verticalCenter
        visible: !root.skeleton

        Text {
            id: caption

            objectName: "lastBarCaption"
            x: 0
            y: (root.firstRowHeight - implicitHeight) / 2
            text: Theme.font.sectionLabel.upperCase ? qsTr("Last").toLocaleUpperCase() : qsTr("Last")
            color: Theme.colour.textSecondary
            font: Theme.font.sectionLabel.font
            Accessible.ignored: true
        }

        Text {
            id: line

            objectName: "lastBarLine"
            x: root.stacked ? 0 : caption.implicitWidth + root.gap
            y: root.stacked ? root.firstRowHeight + root.gap / 2 : (content.height - implicitHeight) / 2
            width: Math.max(0, root.stacked ? chevron.x - root.gap : (count.visible ? count.x : chevron.x) - root.gap - x)
            text: root.hasEntries ? root.lastLine : qsTr("No entries yet")
            color: root.hasEntries ? Theme.colour.textPrimary : Theme.colour.textSecondary
            font: Theme.font.messageText.font
            elide: Text.ElideRight
            maximumLineCount: 1
            Accessible.ignored: true
        }

        Text {
            id: count

            objectName: "lastBarCount"
            // Beside the caption it has all the room it needs; at the largest text sizes it wraps rather than leave the bar.
            width: Math.min(implicitWidth, Math.max(0, content.width - caption.implicitWidth - chevron.width - 3 * root.gap))
            x: chevron.x - root.gap - width
            y: root.stacked ? (root.firstRowHeight - implicitHeight) / 2 : (content.height - implicitHeight) / 2
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignRight
            visible: root.hasEntries
            text: qsTr("%n today", "", root.entryCount)
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            Accessible.ignored: true
        }

        Icon {
            id: chevron

            objectName: "lastBarChevron"
            x: content.width - width
            y: (content.height - height) / 2
            size: root.chevronSize
            kind: "chevronRight"
            colour: Theme.colour.textSecondary
        }
    }

    TapHandler {
        id: tap

        objectName: "lastBarTap"
        enabled: !root.skeleton
        onTapped: root.tapped()
    }
}
