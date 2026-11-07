// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks motion.reduce: a slide of 400 ms that ignores Theme.reduceMotion.
Item {
    Rectangle {
        id: box
        objectName: "box"
        y: 100
        width: 50
        height: 50
        color: "gray"
        NumberAnimation on x {
            objectName: "slide"
            from: 0
            to: 200
            duration: 400
        }
    }
}
