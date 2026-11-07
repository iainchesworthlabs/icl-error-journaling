// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The History tab until package 4.11 (new day and History) is wired in: the title of the tab and one line. It is the one
// file that package replaces in the shell, together with the `historyPage` component of `AppShell.qml`. This file is a part
// of the shell and is tested through it.
Rectangle {
    id: root

    color: Theme.colour.background

    SafeFrame {
        anchors.fill: parent

        NavBar {
            id: bar

            objectName: "historyPlaceholderBar"
            width: parent.width
            title: qsTr("History")
            large: true
        }

        Text {
            objectName: "historyPlaceholderText"
            anchors.top: bar.bottom
            anchors.topMargin: Theme.space.sectionGap
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            wrapMode: Text.Wrap
            color: Theme.colour.textSecondary
            font: Theme.font.body.font
            text: qsTr("Past logs will be listed here.")
        }
    }
}
