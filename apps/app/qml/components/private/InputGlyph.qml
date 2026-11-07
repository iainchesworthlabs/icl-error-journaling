// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The small marks the input and list components draw: a chevron, a check mark, a clear cross, a magnifier and an alert
// sign. They are plain rectangles, so no icon file and no extra Qt module is needed, and they take their size from the
// item (a square) and their colour from `colour`. A mark is decoration: the control that holds it carries the name.
//
// This file is a part of the components that use it and is tested through them.
Item {
    id: root

    // "chevron" (pointing right), "check", "clear", "search" or "alert".
    property string kind: "chevron"
    property color colour: Theme.colour.primaryText

    readonly property real stroke: Theme.border.hairline * 2

    implicitWidth: Theme.space.screenMargin
    implicitHeight: implicitWidth

    Accessible.ignored: true

    // chevron
    Rectangle {
        visible: root.kind === "chevron"
        x: root.width * 0.5 - width / 2
        y: root.height * 0.35 - height / 2
        width: root.width * 0.43
        height: root.stroke
        radius: height / 2
        rotation: 45
        color: root.colour
    }
    Rectangle {
        visible: root.kind === "chevron"
        x: root.width * 0.5 - width / 2
        y: root.height * 0.65 - height / 2
        width: root.width * 0.43
        height: root.stroke
        radius: height / 2
        rotation: -45
        color: root.colour
    }

    // check
    Rectangle {
        visible: root.kind === "check"
        x: root.width * 0.31 - width / 2
        y: root.height * 0.63 - height / 2
        width: root.width * 0.31
        height: root.stroke
        radius: height / 2
        rotation: 45
        color: root.colour
    }
    Rectangle {
        visible: root.kind === "check"
        x: root.width * 0.61 - width / 2
        y: root.height * 0.52 - height / 2
        width: root.width * 0.58
        height: root.stroke
        radius: height / 2
        rotation: -50
        color: root.colour
    }

    // clear
    Rectangle {
        visible: root.kind === "clear"
        x: root.width * 0.5 - width / 2
        y: root.height * 0.5 - height / 2
        width: root.width * 0.7
        height: root.stroke
        radius: height / 2
        rotation: 45
        color: root.colour
    }
    Rectangle {
        visible: root.kind === "clear"
        x: root.width * 0.5 - width / 2
        y: root.height * 0.5 - height / 2
        width: root.width * 0.7
        height: root.stroke
        radius: height / 2
        rotation: -45
        color: root.colour
    }

    // search: a ring and a handle
    Rectangle {
        visible: root.kind === "search"
        x: root.width * 0.08
        y: root.height * 0.08
        width: root.width * 0.62
        height: width
        radius: width / 2
        color: "transparent"
        border.width: root.stroke
        border.color: root.colour
    }
    Rectangle {
        visible: root.kind === "search"
        x: root.width * 0.78 - width / 2
        y: root.height * 0.78 - height / 2
        width: root.width * 0.34
        height: root.stroke
        radius: height / 2
        rotation: 45
        color: root.colour
    }

    // alert: a ring with an exclamation mark
    Rectangle {
        visible: root.kind === "alert"
        anchors.fill: parent
        radius: width / 2
        color: "transparent"
        border.width: root.stroke
        border.color: root.colour
    }
    Rectangle {
        visible: root.kind === "alert"
        x: root.width / 2 - width / 2
        y: root.height * 0.25
        width: root.stroke
        height: root.height * 0.32
        radius: width / 2
        color: root.colour
    }
    Rectangle {
        visible: root.kind === "alert"
        x: root.width / 2 - width / 2
        y: root.height * 0.66
        width: root.stroke
        height: root.stroke
        radius: width / 2
        color: root.colour
    }
}
