// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// One rounded corner of a card of the History list (Stage 4, package 4.11). The rows are square items in a ListView and the
// design has them in a 14 pt card, one card for each date, so each top corner of the first row of a date and each bottom corner
// of its last row is covered by the page colour outside the curve. It is a part of History and is tested through it.
Shape {
    id: root

    // Which corner of the card this is.
    property bool atTop: true
    property bool atLeft: true

    readonly property real radius: Theme.radius.groupedList

    width: radius
    height: radius
    Accessible.ignored: true

    transform: Scale {
        origin.x: root.width / 2
        origin.y: root.height / 2
        xScale: root.atLeft ? 1 : -1
        yScale: root.atTop ? 1 : -1
    }

    // The page colour outside the curve.
    ShapePath {
        strokeColor: "transparent"
        fillColor: Theme.colour.background

        PathSvg {
            path: "M-1 -1L%1 -1L%1 0A%1 %1 0 0 0 0 %1L-1 %1Z".arg(root.radius)
        }
    }
}
