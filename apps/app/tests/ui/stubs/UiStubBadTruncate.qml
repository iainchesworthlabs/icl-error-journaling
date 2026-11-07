// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Breaks a11y.text_scale: a label that is cut after one line and does not elide, so the end of it is lost.
Item {
    Text {
        objectName: "cut"
        x: 20
        y: 100
        width: 100
        maximumLineCount: 1
        text: "A long note that cannot fit on one line of a hundred points"
        wrapMode: Text.WordWrap
        Accessible.name: text
        Accessible.role: Accessible.StaticText
    }
}
