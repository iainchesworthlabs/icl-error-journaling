// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// A small outline icon of a History row (Stage 4, package 4.11): the check or circle of the shared state and the chevron. The
// path is drawn on the 24 point grid of the design's icons and scaled down to the glyph's size. It is decorative: the row says
// the same in words. It is a part of History and is tested through it.
Item {
    id: root

    // An SVG path on a 24 by 24 grid.
    required property string path
    property color colour: Theme.colour.textSecondary

    width: Theme.size.badge * 2 / 3
    height: width
    Accessible.ignored: true

    Shape {
        width: Theme.size.badge
        height: Theme.size.badge
        scale: root.width / Theme.size.badge
        transformOrigin: Item.TopLeft
        Accessible.ignored: true

        ShapePath {
            strokeColor: root.colour
            strokeWidth: Theme.border.hairline * 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.path
            }
        }
    }
}
