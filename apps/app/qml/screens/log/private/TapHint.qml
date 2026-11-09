// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The hint of a single tap on a tile (Stage 4, package 4.17a): a tile is logged by a double-tap, so one tap logs nothing and this
// card says what to do instead, "Double-tap to log". It is the toast's card with the toast's colours, text and shadow, but low
// (the line and 12 pt above and below it), with no tick and no button, and it takes no touch at all: a finger goes through it to
// the tile under it, because the next thing a person does after the hint is to double-tap that tile.
//
// `show()` shows it for `visibleMs` and then lets it fade, and does nothing at all if the last hint started less than
// `intervalMs` ago, so a person who is still learning the gesture is told now and then and not on every tap. Both times are
// Theme.motion values, timed on the animation clock (a NumberAnimation of a number that nothing reads; a lone PauseAnimation
// would follow the wall clock, which a test cannot move). The card fades in and out, with Reduce Motion as well (a fade is what
// it keeps).
//
// The Log screen makes this at the first hint, in a Loader, and keeps it after: a board nobody taps wrongly pays one item
// for it (perf.scene). It stands above the undo toast when that is open.
Item {
    id: root

    property string text: qsTr("Double-tap to log")
    property int visibleMs: Theme.motion.tapHintVisibleMs
    property int intervalMs: Theme.motion.tapHintIntervalMs

    // How many hints have been shown, for the test that the interval holds.
    property int shownCount: 0
    // The hint is on its way in or has arrived, until it starts to fade out.
    readonly property bool active: life.running
    // The card is on screen: while it is active and until it has faded out.
    readonly property bool present: active || card.opacity > 0
    // Numbers that run from 0 to 1 while the hint is shown and while the next one is held back. Nothing reads them.
    property real lifeClock: 0
    property real waitClock: 0

    visible: present
    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: label.implicitHeight + 2 * Theme.space.sectionGap

    // Shows the hint unless one was started less than `intervalMs` ago. Returns whether it did.
    function show(): bool {
        if (waiting.running)
            return false;
        shownCount += 1;
        fadeOut.stop();
        fadeIn.restart();
        life.restart();
        waiting.restart();
        return true;
    }

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
    Accessible.ignored: !root.active

    Item {
        id: card

        objectName: "tapHintCard"
        width: root.width
        height: root.implicitHeight
        opacity: 0

        Shadow {
            anchors.fill: parent
            radius: Theme.radius.toast
            token: Theme.shadow.toast
        }

        Rectangle {
            objectName: "tapHintSurface"
            anchors.fill: parent
            radius: Theme.radius.toast
            color: Theme.colour.toastBackground
            Accessible.ignored: true
        }

        Text {
            id: label

            objectName: "tapHintText"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space.rowPadding
            anchors.rightMargin: Theme.space.rowPadding
            horizontalAlignment: Text.AlignHCenter
            text: root.text
            color: Theme.colour.toastText
            font: Theme.font.messageText.font
            wrapMode: Text.Wrap
            Accessible.ignored: true
        }
    }

    SequentialAnimation {
        id: life

        objectName: "tapHintLife"

        NumberAnimation {
            target: root
            property: "lifeClock"
            from: 0
            to: 1
            duration: root.visibleMs
        }

        ScriptAction {
            script: {
                fadeIn.stop();
                fadeOut.restart();
            }
        }
    }

    NumberAnimation {
        id: waiting

        objectName: "tapHintWait"
        target: root
        property: "waitClock"
        from: 0
        to: 1
        duration: root.intervalMs
    }

    OpacityAnimator {
        id: fadeIn

        objectName: "tapHintFadeIn"
        target: card
        from: 0
        to: 1
        duration: Theme.motion.toastMs
    }

    OpacityAnimator {
        id: fadeOut

        objectName: "tapHintFadeOut"
        target: card
        to: 0
        duration: Theme.motion.toastMs
    }
}
