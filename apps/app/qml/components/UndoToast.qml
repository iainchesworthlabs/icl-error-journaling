// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// The undo toast (Stage 4, package 4.5; BRD-04): a 52 pt card with a tick, the line of the last action, a bar that runs
// down the five-second window, and the Undo button. It shows what UndoController says and asks it nothing: the screen
// binds the five properties below to `undo` and connects undoRequested() to `undo.undo()`.
//
//   active       the toast is showing (undo.active)
//   text         the line, such as "Mute Delay logged at 10:20", or the same with the repeat count (undo.text)
//   undoing      the undo is being committed: the button is disabled until the toast closes (undo.undoing)
//   generation   goes up whenever the window restarts; the bar restarts with it, without a reset (undo.generation)
//   windowMs     the length of the window (undo.windowMs, 5000)
//
// A repeat tap changes `text` and `generation` while `active` stays true, so the card stays where it is and the text
// changes in place. The card slides in with a spring and fades; with Reduce Motion it only fades (Theme.motion). The line
// is announced to VoiceOver once for each generation, and announced(line) says so (assumption U3). The card grows with
// the text size and wraps instead of cutting the line off; 52 pt is its minimum.
//
// The item is as high as the card (implicitHeight), so the screen anchors it by its left, right and bottom and gives it
// no height. The Undo button is named "Undo, <the line>", which starts with its visible label.
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
    // How many times the bar has been started, for the test that it restarts on a new generation and not otherwise.
    property int barRestarts: 0
    property int lastAnnounced: -1
    readonly property real iconSize: Theme.size.toastHeight * 0.4
    readonly property real barHeight: Theme.border.otherWidth * 2

    visible: present
    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: Math.max(Theme.size.toastHeight, row.implicitHeight + 2 * Theme.space.tileGap)

    // The announcement and the bar wait for the end of the change, so that they see the text and the generation together.
    function settleChange() {
        if (!active) {
            lastAnnounced = -1;
            return;
        }
        if (generation === lastAnnounced)
            return;
        lastAnnounced = generation;
        barRestarts += 1;
        countdown.restart();
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
            countdown.stop();
        }
        Qt.callLater(settleChange);
    }
    onGenerationChanged: Qt.callLater(settleChange)
    Component.onCompleted: Qt.callLater(settleChange)

    Accessible.role: Accessible.StaticText
    Accessible.name: root.text
    Accessible.ignored: !root.active

    Item {
        id: card

        objectName: "toastCard"
        width: root.width
        height: root.implicitHeight
        opacity: 0

        Rectangle {
            objectName: "toastSurface"
            anchors.fill: parent
            radius: Theme.radius.toast
            color: Theme.colour.toastBackground
            Accessible.ignored: true
        }

        // The remaining-time bar, along the bottom edge between the rounded corners.
        Item {
            id: track

            objectName: "toastBarTrack"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: Theme.radius.toast
            anchors.rightMargin: Theme.radius.toast
            height: root.barHeight
            clip: true
            visible: !Theme.reduceMotion
            Accessible.ignored: true

            Rectangle {
                id: bar

                objectName: "toastBar"
                width: track.width
                height: track.height
                radius: height / 2
                color: Theme.colour.toastAccent

                XAnimator {
                    id: countdown

                    objectName: "toastBarAnimator"
                    target: bar
                    from: 0
                    to: Theme.reduceMotion ? 0 : -track.width
                    // This is the window itself and not a transition, so Reduce Motion hides the bar and does not speed it up.
                    duration: Theme.reduceMotion ? 0 : root.windowMs
                    easing.type: Easing.Linear
                }
            }
        }

        Row {
            id: row

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.space.rowPadding
            anchors.rightMargin: Theme.space.tileGap
            spacing: Theme.space.tileGap + Theme.border.otherWidth * 2

            Item {
                id: tick

                objectName: "toastTick"
                width: root.iconSize
                height: root.iconSize
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true

                Shape {
                    width: Theme.size.badge
                    height: Theme.size.badge
                    scale: root.iconSize / Theme.size.badge
                    transformOrigin: Item.TopLeft

                    ShapePath {
                        strokeColor: Theme.colour.toastAccent
                        strokeWidth: Theme.border.otherWidth * 5 / 3
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin

                        PathSvg {
                            path: "M5 12l5 5L20 7"
                        }
                    }
                }
            }

            Text {
                id: line

                objectName: "toastText"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(0, row.width - tick.width - undoButton.width - 2 * row.spacing)
                text: root.text
                color: Theme.colour.toastText
                font: Theme.font.bodySmall.font
                wrapMode: Text.Wrap
                Accessible.ignored: true
            }

            Rectangle {
                id: undoButton

                objectName: "toastUndo"
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(Theme.size.minTouchTarget, undoLabel.implicitWidth + 2 * Theme.space.rowPadding)
                height: Math.max(Theme.size.minTouchTarget, undoLabel.implicitHeight + Theme.space.tileGap)
                radius: Theme.radius.field - Theme.border.otherWidth * 2
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
                    font: Theme.font.bodySmall.selectedFont
                    Accessible.ignored: true
                }

                TapHandler {
                    objectName: "toastUndoTap"
                    enabled: !root.undoing
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
