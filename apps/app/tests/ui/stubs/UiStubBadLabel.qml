// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks a11y.label_in_name: a button whose name is another word than its label, a row of two lines whose name drops
// the second, and a tab whose name is not its label. Voice Control's "Tap Add" and "Tap Sam Example" find none of them.
Item {
    Rectangle {
        objectName: "plus"
        x: 20
        y: 100
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Plus"

        Text {
            anchors.centerIn: parent
            text: "Add"
            Accessible.ignored: true
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "twoLines"
        x: 20
        y: 200
        width: 300
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Sam Example"

        Text {
            x: 8
            y: 4
            text: "Sam Example"
            Accessible.ignored: true
        }

        Text {
            x: 8
            y: 28
            text: "Wrong Response on UNIT"
            Accessible.ignored: true
        }

        TapHandler {}
    }
    Rectangle {
        objectName: "tab"
        x: 20
        y: 300
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.PageTab
        Accessible.name: "Third"

        Text {
            anchors.centerIn: parent
            text: "Review"
            Accessible.ignored: true
        }

        TapHandler {}
    }
}
