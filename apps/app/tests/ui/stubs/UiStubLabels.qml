// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Follows a11y.label_in_name in the shapes the app uses: a name in another case, a tile that reads "<label>, <n> today", a
// row that adds its value, a button with an icon and no label, a count with no letter in it, a control with a nested
// control, and a label that is long and would elide.
Item {
    Rectangle {
        objectName: "shout"
        x: 20
        y: 100
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "ADD"

        Text {
            anchors.centerIn: parent
            text: "Add"
            Accessible.ignored: true
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "tile"
        x: 20
        y: 200
        width: 120
        height: 104
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Wrong Response, 2 today"

        Text {
            x: 8
            y: 8
            text: "Wrong  Response"
            Accessible.ignored: true
        }

        Text {
            x: 8
            y: 60
            text: "2"
            Accessible.ignored: true
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "row"
        x: 160
        y: 200
        width: 220
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Location, North Site"

        Text {
            x: 8
            y: 4
            text: "Location"
            Accessible.ignored: true
        }

        Text {
            x: 120
            y: 4
            text: "North Site"
            Accessible.ignored: true
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "iconOnly"
        x: 20
        y: 330
        width: 54
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Close"

        Text {
            anchors.centerIn: parent
            text: "×"
            Accessible.ignored: true
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "card"
        x: 100
        y: 330
        width: 280
        height: 80
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Sam Example"

        Text {
            x: 8
            y: 4
            text: "Sam Example"
            Accessible.ignored: true
        }

        // A control of its own inside the card: its text is its own and is not asked of the card.
        Rectangle {
            objectName: "cardUndo"
            x: 170
            y: 20
            width: 100
            height: 54
            color: "lightgray"
            Accessible.role: Accessible.Button
            Accessible.name: "Undo, Wrong Response"

            Text {
                anchors.centerIn: parent
                text: "Undo"
                Accessible.ignored: true
            }

            TapHandler {}
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "long"
        x: 20
        y: 430
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Sequence Out of Order"

        Text {
            anchors.fill: parent
            elide: Text.ElideRight
            text: "Sequence Out of Order"
            Accessible.ignored: true
        }

        TapHandler {}
    }
}
