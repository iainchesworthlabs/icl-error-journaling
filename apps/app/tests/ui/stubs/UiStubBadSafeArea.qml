// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks a11y.safe_area: a button 10 pt from the top of the window, inside the Dynamic Island.
Item {
    Rectangle {
        objectName: "island"
        x: 20
        y: 10
        width: 120
        height: 54
        color: "gray"
        Accessible.role: Accessible.Button
        Accessible.name: "Under the island"
        TapHandler {}
    }
}
