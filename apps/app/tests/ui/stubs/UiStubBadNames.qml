// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks a11y.names: a button without a name, and two buttons with the same name and value.
Item {
    Rectangle {
        objectName: "unnamed"
        x: 20
        y: 100
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        TapHandler {}
    }
    Rectangle {
        objectName: "first"
        x: 20
        y: 200
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Add"
        TapHandler {}
    }
    Rectangle {
        objectName: "second"
        x: 200
        y: 200
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Add"
        TapHandler {}
    }
    Rectangle {
        objectName: "silent"
        x: 20
        y: 300
        width: 120
        height: 54
        color: "gray"
        TapHandler {}
    }
}
