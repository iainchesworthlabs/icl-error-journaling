// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// The small line drawings of the editors: the arrows of "Move earlier" and "Move later", the plus of an add row, the
// minus of "Remove" and the six dots of a drag handle. They are drawn in a 24-unit square and take their size from
// `Theme.size.badge`. A drawing is decoration: the button that holds it carries the name.
//
// This file is a part of the editors and is tested through them.
Item {
    id: root

    // "up", "down", "plus", "remove" or "handle".
    property string kind: "plus"
    property color colour: Theme.colour.primaryText

    readonly property string path: kind === "up" ? "M12 19V5M5 12L12 5L19 12" : kind === "down" ? "M12 5V19M5 12L12 19L19 12" : kind === "remove" ? "M5 12H19" : kind === "handle" ? "M9 6H9.01M15 6H15.01M9 12H9.01M15 12H15.01M9 18H9.01M15 18H15.01" : "M12 5V19M5 12H19"

    implicitWidth: Theme.size.badge
    implicitHeight: Theme.size.badge
    Accessible.ignored: true

    Shape {
        width: Theme.size.badge
        height: Theme.size.badge
        scale: root.width / Theme.size.badge
        transformOrigin: Item.TopLeft
        Accessible.ignored: true

        ShapePath {
            strokeColor: root.colour
            strokeWidth: root.kind === "handle" ? Theme.border.hairline * 3 : Theme.border.hairline * 2
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin

            PathSvg {
                path: root.path
            }
        }
    }
}
