// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// Follows motion.reduce: the slide and its length come from Theme.motion, so with Reduce Motion on it is a fade of 100 ms.
Item {
    Rectangle {
        id: box

        objectName: "box"
        y: 100
        width: 50
        height: 50
        color: Theme.colour.primary

        XAnimator on x {
            objectName: "slide"
            from: Theme.motion.travel(200)
            to: 0
            duration: Theme.motion.sheetPresentMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator on opacity {
            objectName: "fade"
            from: 0
            to: 1
            duration: Theme.motion.sheetPresentMs
        }
    }
}
