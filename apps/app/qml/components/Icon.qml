// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// One icon of the design, drawn from the mock-ups' own SVG paths (docs/design/screens) on their 24-unit grid: stroked,
// with round caps and joins, at any size and in any colour. It is decoration: the control that holds an icon carries
// the accessible name, so the icon is ignored by a screen reader.
//
//     Icon { kind: "chevronRight"; size: 14; colour: Theme.colour.borderStrong }
//
//   kind         the drawing, one of the names below
//   size         the side of the square in points (24 by default); the paths scale to it, and so does the stroke
//   colour       the stroke colour (primaryText by default)
//   strokeWidth  the stroke in units of the 24 grid, so 2 is 2 pt at 24 pt and about 1.2 pt at 14 pt. Each kind has the
//                width its mock-up uses (`defaultStroke`); set it to draw a kind heavier or lighter, as the back chevron
//                (2.2) and the plus of the new-log row (2.2) do.
//
// Kinds: chevronRight, chevronLeft, plus, close, check, warning, info, ring (an empty circle), search, share (upload),
// file, list, music, mic (dictation), speech (a speech bubble), calendar, grip (six dots), copy, trash, swap (Change error type), board (the
// Log tab: four rounded squares), help (a question mark in a circle), external (an arrow leaving a box), sun (the Settings tab: a ring with eight rays). The tabs use list for Review and
// calendar for History. An unknown kind draws nothing.
Item {
    id: root

    property string kind: "chevronRight"
    property real size: Theme.size.badge
    property color colour: Theme.colour.primaryText
    property real strokeWidth: defaultStroke

    // The paths, as the mock-ups have them. A circle or a rectangle of the SVG is written as arcs and lines.
    readonly property var paths: ({
            "chevronRight": "M9 6l6 6-6 6",
            "chevronLeft": "M15 18l-6-6 6-6",
            "plus": "M12 5v14M5 12h14",
            "close": "M6 6l12 12M18 6L6 18",
            "check": "M5 12l5 5L20 7",
            "warning": "M12 3L2 21h20L12 3z M12 10v5M12 18h.01",
            "info": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0 -18 0z M12 11v6M12 7.5h.01",
            "ring": "M4 12a8 8 0 1 0 16 0a8 8 0 1 0 -16 0z",
            "search": "M4 11a7 7 0 1 0 14 0a7 7 0 1 0 -14 0z M21 21l-4.3-4.3",
            "share": "M12 3v12M7 8l5-5 5 5 M5 13v6a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-6",
            "file": "M14 3H7a2 2 0 0 0-2 2v14a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2V8l-5-5z M14 3v5h5",
            "list": "M8 6h13M8 12h13M8 18h13M3 6h.01M3 12h.01M3 18h.01",
            "music": "M9 18V5l12-2v13 M3 18a3 3 0 1 0 6 0a3 3 0 1 0 -6 0z M15 16a3 3 0 1 0 6 0a3 3 0 1 0 -6 0z",
            "mic": "M12 1a3 3 0 0 0 -3 3v8a3 3 0 0 0 6 0v-8a3 3 0 0 0 -3 -3z M19 10v2a7 7 0 0 1 -14 0v-2 M12 19v4 M8 23h8",
            "speech": "M21 15a2 2 0 0 1-2 2H7l-4 4V5a2 2 0 0 1 2-2h14a2 2 0 0 1 2 2z",
            "calendar": "M5 4h14a2 2 0 0 1 2 2v13a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2z M16 2v4M8 2v4M3 10h18",
            "grip": "M9 6h.01M15 6h.01M9 12h.01M15 12h.01M9 18h.01M15 18h.01",
            "copy": "M11 9h8a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-8a2 2 0 0 1-2-2v-8a2 2 0 0 1 2-2z M5 15H4a1 1 0 0 1-1-1V4a1 1 0 0 1 1-1h10a1 1 0 0 1 1 1v1",
            "trash": "M4 7h16M10 11v6M14 11v6M6 7l1 13h10l1-13M9 7V4h6v3",
            "swap": "M7 4l-4 4 4 4M3 8h14M17 20l4-4-4-4M21 16H7",
            "board": "M4.5 3h4a1.5 1.5 0 0 1 1.5 1.5v4a1.5 1.5 0 0 1-1.5 1.5h-4a1.5 1.5 0 0 1-1.5-1.5v-4a1.5 1.5 0 0 1 1.5-1.5z " + "M15.5 3h4a1.5 1.5 0 0 1 1.5 1.5v4a1.5 1.5 0 0 1-1.5 1.5h-4a1.5 1.5 0 0 1-1.5-1.5v-4a1.5 1.5 0 0 1 1.5-1.5z " + "M4.5 14h4a1.5 1.5 0 0 1 1.5 1.5v4a1.5 1.5 0 0 1-1.5 1.5h-4a1.5 1.5 0 0 1-1.5-1.5v-4a1.5 1.5 0 0 1 1.5-1.5z " + "M15.5 14h4a1.5 1.5 0 0 1 1.5 1.5v4a1.5 1.5 0 0 1-1.5 1.5h-4a1.5 1.5 0 0 1-1.5-1.5v-4a1.5 1.5 0 0 1 1.5-1.5z",
            "help": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0 -18 0z M9.6 9.4a2.5 2.5 0 1 1 3.5 2.3c-.7.4-1.1.9-1.1 1.7 M12 17.2h.01",
            "external": "M14 4h6v6 M20 4l-9 9 M18 14v5a1 1 0 0 1-1 1H5a1 1 0 0 1-1-1V7a1 1 0 0 1 1-1h5",
            "sun": "M9 12a3 3 0 1 0 6 0a3 3 0 1 0 -6 0z M12 2v3M12 19v3M4.2 4.2l2.1 2.1M17.7 17.7l2.1 2.1M2 12h3M19 12h3M4.2 19.8l2.1-2.1M17.7 6.3l2.1-2.1"
        })
    // The stroke a kind has in the mock-ups: 2.5 for the check, 3 for the dots of the grip, 2 for the rest.
    readonly property real defaultStroke: kind === "check" ? Theme.border.otherWidth * 5 / 3 : kind === "grip" ? Theme.border.otherWidth * 2 : Theme.border.hairline * 2
    // The side of the grid the paths are drawn on.
    readonly property real grid: Theme.size.badge
    readonly property bool known: paths[kind] !== undefined

    implicitWidth: size
    implicitHeight: size
    Accessible.ignored: true

    Shape {
        anchors.fill: parent
        preferredRendererType: Shape.CurveRenderer

        ShapePath {
            strokeColor: root.colour
            strokeWidth: root.strokeWidth * root.width / root.grid
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            scale: Qt.size(root.width / root.grid, root.height / root.grid)

            PathSvg {
                path: root.known ? root.paths[root.kind] : ""
            }
        }
    }
}
