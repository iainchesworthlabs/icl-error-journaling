// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The foot of the Settings list (Stage 4, 4.15): the version of the app and which bridge and storage it was built with, in
// small type. Five quick taps on it show or hide the timing line the exit gate is read from on a phone (Stage 5, "Timing
// marks"): `launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms`. There is no Mac to read the marks with, so
// this line is how the numbers get off a TestFlight build. It is not announced as a control and has no hint of the gesture.
//
// This file is a part of SettingsScreen and is tested through it (and through the shell, in app_flow_test.cpp).
Item {
    id: root

    // The version and platform text, such as "0.1.0 · ios, storage: sqlite". Empty hides the whole footer.
    property string aboutText
    // The timing line from the bridge.
    property string timingText
    // Whether the timing line is shown.
    property bool timingShown: false

    // The taps that show or hide the line, counted by the pointer handler.
    readonly property int tapsToToggle: 5

    visible: aboutText.length > 0
    width: parent ? parent.width : 0
    height: visible ? Math.max(Theme.size.minTouchTarget, column.implicitHeight) : 0
    Accessible.role: Accessible.StaticText
    Accessible.name: root.timingShown ? root.aboutText + ". " + root.timingText : root.aboutText

    TapHandler {
        onTapped: {
            if (tapCount >= root.tapsToToggle)
                root.timingShown = !root.timingShown;
        }
    }

    Column {
        id: column

        width: parent.width
        spacing: Theme.space.tileGap / 2

        Text {
            objectName: "settingsAbout"
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: root.aboutText
            Accessible.ignored: true
        }

        Text {
            objectName: "settingsTiming"
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            visible: root.timingShown
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: root.timingText
            Accessible.ignored: true
        }
    }
}
