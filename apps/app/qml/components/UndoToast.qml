// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The undo toast (Stage 4, package 4.5; BRD-04): a 52 pt card with a tick, the line of the last action and the Undo
// button, as the mock-ups draw it (no countdown bar: the owner chose the mock-up). Look: 14 pt padding on the left and
// 8 pt on the right, 10 pt gaps, a 20 pt tick, the line in 14 pt, the Undo button 40 pt high (a 44 pt touch area through
// its tap margin) with 14 pt padding, a 10 pt radius and a 15/600 label, and the toast shadow. It shows what UndoController says and asks it nothing: the screen
// binds the five properties below to `undo` and connects undoRequested() to `undo.undo()`.
//
//   active       the toast is showing (undo.active)
//   text         the line, such as "Mute Delay logged at 10:20", or the same with the repeat count (undo.text)
//   undoing      the undo is being committed: the button is disabled until the toast closes (undo.undoing)
//   generation   goes up whenever the window restarts (undo.generation): the line is announced again
//   windowMs     the length of the window (undo.windowMs, 5000); the controller times it, the toast only shows it
//
// A repeat tap changes `text` and `generation` while `active` stays true, so the card stays where it is and the text
// changes in place. The card slides in with a spring and fades; with Reduce Motion it only fades (Theme.motion). The line
// is announced to VoiceOver once for each generation, and announced(line) says so (assumption U3). The card grows with
// the text size and wraps instead of cutting the line off; 52 pt is its minimum.
//
// The item is as high as the card (implicitHeight), so the screen anchors it by its left, right and bottom and gives it
// no height. The card takes every touch on it, so nothing under the toast is pressed through it. The Undo button is
// named "Undo, <the line>", which starts with its visible label.
Item {
    id: root

    required property bool active
    required property string text
    required property bool undoing
    required property int generation
    required property int windowMs

    signal undoRequested
    // The line was handed to the screen reader: once for each generation of an open toast.
    signal announced(string line)

    // The card is on screen: while the toast is open and until it has faded out.
    readonly property bool present: active || card.opacity > 0
    // How many windows have started: one for each new generation of an open toast, for the test that a repeat tap in
    // the same generation does not start another.
    property int windowStarts: 0
    property int lastAnnounced: -1
    readonly property real iconSize: Theme.size.badge * 5 / 6

    visible: present
    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: Math.max(Theme.size.toastHeight, row.implicitHeight + Theme.space.sectionGap)

    // The announcement waits for the end of the change, so that it sees the text and the generation together.
    function settleChange() {
        if (!active) {
            lastAnnounced = -1;
            return;
        }
        if (generation === lastAnnounced)
            return;
        lastAnnounced = generation;
        windowStarts += 1;
        Accessible.announce(text);
        announced(text);
    }

    onActiveChanged: {
        if (active) {
            fadeOut.stop();
            slideOut.stop();
            fadeIn.restart();
            slideIn.restart();
        } else {
            fadeIn.stop();
            slideIn.stop();
            fadeOut.restart();
            slideOut.restart();
        }
        Qt.callLater(settleChange);
    }
    onGenerationChanged: Qt.callLater(settleChange)
    Component.onCompleted: Qt.callLater(settleChange)

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
    Accessible.ignored: !root.active

    // The whole toast takes the touch: a press, a release and a tap on it go no further, so the tile under a toast that
    // floats over the board is not pressed (and no entry recorded) by a tap on the toast. This handler takes the grab at
    // the press. It sits on the item that is already announced as the toast's line, so it is no control of its own, and
    // Undo stays the only button.
    TapHandler {
        objectName: "toastShield"
        enabled: root.active
        gesturePolicy: TapHandler.WithinBounds
    }

    Item {
        id: card

        objectName: "toastCard"
        width: root.width
        height: root.implicitHeight
        opacity: 0

        Shadow {
            anchors.fill: parent
            radius: Theme.radius.toast
            token: Theme.shadow.toast
        }

        Rectangle {
            objectName: "toastSurface"
            anchors.fill: parent
            radius: Theme.radius.toast
            color: Theme.colour.toastBackground
            Accessible.ignored: true
        }

        Row {
            id: row

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space.rowPadding
            anchors.rightMargin: Theme.space.tileGap
            spacing: Theme.space.tileGap + Theme.border.hairline * 2

            Icon {
                id: tick

                objectName: "toastTick"
                anchors.verticalCenter: parent.verticalCenter
                size: root.iconSize
                kind: "check"
                colour: Theme.colour.toastAccent
            }

            Text {
                id: line

                objectName: "toastText"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, row.width - tick.width - undoButton.width - 2 * row.spacing)
                text: root.text
                color: Theme.colour.toastText
                font: Theme.font.messageText.font
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            Rectangle {
                id: undoButton

                objectName: "toastUndo"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(Theme.size.minTouchTarget, undoLabel.implicitWidth + 2 * Theme.space.rowPadding)
                // 40 pt drawn; the tap handler's margin makes the touch area 44.
                height: Math.max(Theme.size.minTouchTarget - Theme.border.hairline * 4, undoLabel.implicitHeight + Theme.space.tileGap)
                radius: Theme.radius.small
                color: Theme.colour.toastButton
                opacity: root.undoing ? 0.5 : 1
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Undo, %1").arg(root.text)
                Accessible.ignored: !root.active
                Accessible.focusable: true
                Accessible.onPressAction: {
                    if (!root.undoing)
                        root.undoRequested();
                }

                Text {
                    id: undoLabel

                    anchors.centerIn: parent
                    text: qsTr("Undo")
                    color: Theme.colour.toastAccent
                    font: Theme.font.buttonCompact.font
                    Accessible.ignored: true
                }

                TapHandler {
                    objectName: "toastUndoTap"
                    enabled: !root.undoing
                    margin: Math.max(0, (Theme.size.minTouchTarget - undoButton.height) / 2)
                    onTapped: root.undoRequested()
                }
            }
        }
    }

    OpacityAnimator {
        id: fadeIn

        objectName: "toastFadeIn"
        target: card
        from: 0
        to: 1
        duration: Theme.motion.toastMs
    }

    YAnimator {
        id: slideIn

        objectName: "toastSlideIn"
        target: card
        from: Theme.motion.travel(Theme.size.toastHeight)
        to: 0
        duration: Theme.motion.toastMs
        easing.type: Theme.motion.easingSpring
    }

    OpacityAnimator {
        id: fadeOut

        objectName: "toastFadeOut"
        target: card
        to: 0
        duration: Theme.motion.toastMs
    }

    YAnimator {
        id: slideOut

        objectName: "toastSlideOut"
        target: card
        to: Theme.motion.travel(Theme.size.toastHeight)
        duration: Theme.motion.toastMs
        easing.type: Easing.InQuad
    }
}
