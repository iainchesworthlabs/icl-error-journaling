// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks a11y.targets: a named button 30 pt square, and one that is 30 pt but has a 8 pt handler margin (44 pt hit area).
Item {
    Rectangle {
        objectName: "small"
        x: 20
        y: 100
        width: 30
        height: 30
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Small"
        TapHandler {}
    }
    Rectangle {
        objectName: "padded"
        x: 20
        y: 200
        width: 30
        height: 30
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Padded"
        TapHandler {
            margin: 7
        }
    }
}
