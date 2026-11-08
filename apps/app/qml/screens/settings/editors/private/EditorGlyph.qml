// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// The small line drawings of the editors: the arrows of "Move earlier" and "Move later", the plus of an add row, the
// cross of "Remove" and the six dots of a drag handle. They are drawn in a 24-unit square and shown 20 pt wide, as the
// mock-ups have them; the stroke is 2.2 on the grid (3 for the dots), as the mock-ups have it.
// A drawing is decoration: the button that holds it carries the name.
//
// This file is a part of the editors and is tested through them.
Item {
    id: root

    // "up", "down", "plus", "remove" or "handle".
    property string kind: "plus"
    property color colour: Theme.colour.primaryText

    readonly property string path: kind === "up" ? "M12 19V5M5 12L12 5L19 12" : kind === "down" ? "M12 5V19M5 12L12 19L19 12" : kind === "remove" ? "M6 6l12 12M18 6L6 18" : kind === "handle" ? "M9 6H9.01M15 6H15.01M9 12H9.01M15 12H15.01M9 18H9.01M15 18H15.01" : "M12 5V19M5 12H19"

    implicitWidth: Theme.size.badge * 5 / 6
    implicitHeight: implicitWidth
    Accessible.ignored: true

    Shape {
        // As Icon draws: the path is scaled and not the item, and the curve renderer is used.
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer
        Accessible.ignored: true

        ShapePath {
            strokeColor: root.colour
            strokeWidth: (root.kind === "handle" ? Theme.border.hairline * 3 : Theme.border.hairline * 2.2) * root.width / Theme.size.badge
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(root.width / Theme.size.badge, root.height / Theme.size.badge)

            PathSvg {
                path: root.path
            }
        }
    }
}
