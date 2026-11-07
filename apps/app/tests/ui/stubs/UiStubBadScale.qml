// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// Breaks a11y.text_scale: a label in a box that does not grow, which is cut at a large text size.
Item {
    Text {
        objectName: "label"
        x: 20
        y: 100
        width: 380
        height: 24
        font: Theme.font.body.font
        text: "Sequence Out of Order"
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
