// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The empty application: one window with a placeholder where the board will be (a grid three across and four down in
// portrait, docs/design). The board stays inside the safe area, clear of the notch and the home indicator.
Window {
    id: root

    // Given by main.cpp: the version of the core library, and which bridge and storage are linked.
    required property string versionText
    required property string platformText

    width: 390
    height: 844
    visible: true
    title: qsTr("Error Journaling")
    color: "#f4f1ea"

    Item {
        id: safeArea

        anchors.fill: parent
        anchors.leftMargin: SafeArea.margins.left
        anchors.rightMargin: SafeArea.margins.right
        anchors.topMargin: SafeArea.margins.top
        anchors.bottomMargin: SafeArea.margins.bottom

        Rectangle {
            id: board

            objectName: "board"
            anchors.fill: parent
            anchors.margins: 16
            radius: 12
            color: "transparent"
            border.color: "#8a8478"
            border.width: 2

            Text {
                anchors.centerIn: parent
                horizontalAlignment: Text.AlignHCenter
                color: "#4a463e"
                font.pixelSize: 20
                text: qsTr("Board") + "\n" + root.versionText + "\n" + root.platformText
            }
        }
    }
}
