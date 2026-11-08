// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The white group of rows with 14 pt corners, under an optional small heading and above an optional note. Put ListRows
// (or SwipeRows, or any item) in it; they are stacked top to bottom at the full width of the group. The group is as
// tall as its rows, whatever their text size.
//
//     GroupedList {
//         title: qsTr("Location")
//         ListRow { text: qsTr("North Site"); checkable: true; checked: true }
//         ListRow { text: qsTr("South Site"); checkable: true }
//     }
//
// The heading is 6 pt above the card and indented by `headingInset` (4 pt, the form labels; 16 pt for the Other
// picker's section headings). The footer is 13 pt with a 1.4 line height, 10 pt under the card.
Item {
    id: root

    // The heading above the group ("Location"), drawn small and in capitals. Empty = none.
    property string title: ""
    // A note under the group ("Type the number only."). Empty = none.
    property string footer: ""
    // The surface behind the rows. A screen on a sheet sets the raised surface.
    property color surface: Theme.colour.surface
    // How far the heading is indented from the card's edge.
    property real headingInset: Theme.space.tileGap / 2

    default property alias rows: column.data

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: heading.height + card.height + footnote.height

    Accessible.role: Accessible.Grouping
    Accessible.name: root.title.length > 0 ? root.title : qsTr("List")

    Text {
        id: heading

        x: root.headingInset
        width: parent.width - 2 * x
        height: visible ? implicitHeight + Theme.space.tileGap * 3 / 4 : 0
        visible: root.title.length > 0
        verticalAlignment: Text.AlignTop
        color: Theme.colour.textSecondary
        font: Theme.font.sectionLabel.font
        text: root.title.toLocaleUpperCase()
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    Rectangle {
        id: card

        y: heading.height
        width: parent.width
        height: column.height
        radius: Theme.radius.groupedList
        color: root.surface

        Column {
            id: column

            width: parent.width
        }
    }

    Text {
        id: footnote

        x: Theme.space.tileGap / 2
        y: card.y + card.height
        width: parent.width - 2 * x
        height: visible ? implicitHeight + Theme.space.tileGap + Theme.border.hairline * 2 : 0
        visible: root.footer.length > 0
        verticalAlignment: Text.AlignBottom
        color: Theme.colour.textSecondary
        font: Theme.font.footnote.font
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.font.footnote.linePitch
        text: root.footer
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
