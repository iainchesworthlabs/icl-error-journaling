// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// One rounded corner of the Review card (Stage 4, package 4.10). The rows of the timeline are square items in a list, and the
// design has them in a 14 pt card, so each of the first row's top corners and the last row's bottom corners is covered by
// the page colour and has the card's hairline drawn along the curve. It is a part of Review and is tested through it.
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
        fillColor: Theme.colour.reviewBackground

        PathSvg {
            path: "M-1 -1L%1 -1L%1 0A%1 %1 0 0 0 0 %1L-1 %1Z".arg(root.radius)
        }
    }

    // The hairline of the card along the curve.
    ShapePath {
        strokeColor: Theme.colour.reviewDivider
        strokeWidth: Theme.border.hairline
        fillColor: "transparent"

        PathSvg {
            path: "M%1 0A%1 %1 0 0 0 0 %1".arg(root.radius)
        }
    }
}
