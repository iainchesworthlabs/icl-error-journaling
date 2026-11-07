// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// A component that follows every rule of the cross-cutting tests, for the tests of the rig and of the checks themselves:
// a title, a 54 pt button with a name and a role, a hint under it, all in Theme values, inside the safe frame. The
// UiStubBad* files each break one rule.
Item {
    id: root

    required property string title
    property string buttonLabel: qsTr("Add")
    property int presses: 0

    signal activated

    SafeFrame {
        anchors.fill: parent
        overrideInsets: true
        overrideTop: Theme.frame.safeAreaTop
        overrideBottom: Theme.frame.homeIndicator

        Rectangle {
            anchors.fill: parent
            color: Theme.colour.background
        }

        Text {
            id: titleText

            objectName: "title"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: Theme.space.screenMargin
            color: Theme.colour.textPrimary
            font: Theme.font.sheetTitle.font
            text: root.title
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: root.title
        }

        Rectangle {
            id: button

            objectName: "activate"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: titleText.bottom
            anchors.margins: Theme.space.screenMargin
            height: Theme.size.primaryButtonHeight
            radius: Theme.radius.primaryButton
            color: tap.pressed ? Theme.colour.primaryPressed : Theme.colour.primary
            Accessible.role: Accessible.Button
            Accessible.name: root.buttonLabel
            Accessible.onPressAction: root.activated()

            Text {
                anchors.centerIn: parent
                width: parent.width - 2 * Theme.space.screenMargin
                horizontalAlignment: Text.AlignHCenter
                color: Theme.colour.textOnPrimary
                font: Theme.font.body.font
                text: root.buttonLabel
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            TapHandler {
                id: tap

                onPressedChanged: {
                    if (pressed) {
                        root.presses += 1;
                        root.activated();
                    }
                }
            }
        }

        Text {
            objectName: "hint"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: button.bottom
            anchors.margins: Theme.space.screenMargin
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: qsTr("Sam Example logged a Wrong Response on UNIT at North Site.")
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
}
