// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks a11y.reading_order: the lower text is declared first, so a screen reader reads it first.
Item {
    Text {
        objectName: "lower"
        x: 20
        y: 400
        width: 200
        text: "Lower"
        Accessible.role: Accessible.StaticText
        Accessible.name: "Lower"
    }
    Text {
        objectName: "upper"
        x: 20
        y: 100
        width: 200
        text: "Upper"
        Accessible.role: Accessible.StaticText
        Accessible.name: "Upper"
    }
}
