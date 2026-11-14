// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The application window (Stage 4, package 4.15). It is the window and nothing more: the colour of the first frame, the
// title, and the values `main.cpp` gives it as initial properties, passed on to the shell (shell/AppShell.qml), which is
// everything inside. They are kept apart because the test rig loads an item and not a window; `app_flow_test.cpp` drives the
// shell as the window holds it.
//
// Given by main.cpp: the nine services of AppRoot (Stage 3, 3.11), AppRoot itself for its one write-failed signal, the
// bridge to what Qt does not cover on an iPhone, the model of the About pages, and the version and platform text of the build.
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
    // The model of the About pages: the build's commit and date, the licence and the open-source components.
    required property QtObject about
    // What the in-app help remembers (HelpState): the first-run tip. Without it the tip is not shown.
    property QtObject help: null
    // Which boards there are and which was used last (BoardState), and what the Interactions board shows. Without them the app is
    // the one of v1: four tabs.
    property QtObject boards: null
    property QtObject interactionBoard: null
    // How shared text is written (ExportPreferences): the level of a name (nameExport) and the label of an interaction line.
    property QtObject exportPreferences: null
    required property var bridge
    // The version of the core library and which bridge and storage are linked. They are at the foot of Settings.
    required property string versionText
    required property string platformText

    width: Theme.frame.width
    height: Theme.frame.height
    // The smallest window the layout is laid out and tested for (Stage 8): a desktop honours it, and main.cpp tells iPadOS
    // (Stage Manager can make a window smaller than any phone).
    minimumWidth: Theme.layout.minimumWindowWidth
    minimumHeight: Theme.layout.minimumWindowHeight
    visible: true
    // The window reaches under the status bar and the home indicator, and SafeFrame keeps the content clear of them. Without
    // the hint, iOS gives the window only the safe area and draws black above and below it, and the tab bar ends above
    // the home indicator instead of at the bottom of the screen.
    flags: Qt.Window | Qt.ExpandedClientAreaHint
    title: qsTr("Field Test")
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
        about: root.about
        help: root.help
        boards: root.boards
        interactionBoard: root.interactionBoard
        exportPreferences: root.exportPreferences
        aboutText: qsTr("%1 · %2").arg(root.versionText).arg(root.platformText)
    }
}
