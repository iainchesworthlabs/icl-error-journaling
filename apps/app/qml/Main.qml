// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The application window (Stage 4, package 4.15). It is the window and nothing more: the colour of the first frame, the
// title, and the values `main.cpp` gives it as initial properties, passed on to the shell (shell/AppShell.qml), which is
// everything inside. They are kept apart because the test rig loads an item and not a window; `app_flow_test.cpp` drives the
// shell as the window holds it.
//
// Given by main.cpp: the nine services of AppRoot (Stage 3, 3.11), AppRoot itself for its one write-failed signal, the
// bridge to what Qt does not cover on an iPhone, and the version and platform text of the build.
Window {
    id: root

    required property QtObject session
    required property QtObject undo
    required property QtObject board
    required property QtObject picker
    required property QtObject timeline
    required property QtObject history
    required property QtObject day
    required property QtObject settings
    required property QtObject share
    required property QtObject appRoot
    required property var bridge
    // The version of the core library and which bridge and storage are linked. They are at the foot of Settings.
    required property string versionText
    required property string platformText

    width: Theme.frame.width
    height: Theme.frame.height
    visible: true
    title: qsTr("CS Bingo Board")
    // The colour of the launch screen's background, so that the hand-over to the first screen shows no flash.
    color: Theme.colour.background

    AppShell {
        anchors.fill: parent

        // 5.7: the launch time ends when the 12 tiles accept taps.
        onBoardEnabledChanged: {
            if (boardEnabled)
                root.bridge.reportBoardEnabled();
        }
        session: root.session
        undo: root.undo
        board: root.board
        picker: root.picker
        timeline: root.timeline
        history: root.history
        day: root.day
        settings: root.settings
        share: root.share
        appRoot: root.appRoot
        bridge: root.bridge
        aboutText: qsTr("%1 · %2").arg(root.versionText).arg(root.platformText)
    }
}
