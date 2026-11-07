// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The empty application: one window with a placeholder where the board will be (a grid three across and four down in
// portrait, docs/design). The board stays inside the safe area, clear of the notch and the home indicator.
Window {
    id: root

    // Given by main.cpp: the version of the core library, and which bridge and storage are linked.
    required property string versionText
    required property string platformText
    // The bridge to what Qt does not cover on an iPhone (the share sheet, haptics); see docs/plan/spikes.md.
    required property QtObject bridge
    // The fixed fictional text the share test sends.
    readonly property string spikeShareText: "Sam Example logged a Wrong Response on UNIT at North Site."

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

            // Stage 0 spike controls (docs/plan/spikes.md, "Only a device can confirm"). They exercise the share
            // sheet and the two haptics on a phone; Stage 4 replaces this window with the real screens.
            Column {
                anchors.centerIn: parent
                width: parent.width - 32
                spacing: 16

                Text {
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: "#4a463e"
                    font.pixelSize: 20
                    text: qsTr("Board") + "\n" + root.versionText + "\n" + root.platformText
                }

                SpikeTile {
                    objectName: "impactTile"
                    label: qsTr("Impact on press")
                    tileColour: "#d9c7e8"
                    onPressedDown: root.bridge.hapticImpact()
                }

                SpikeTile {
                    objectName: "successTile"
                    label: qsTr("Success on tap")
                    tileColour: "#c7dcef"
                    onTapped: root.bridge.hapticSuccess()
                }

                SpikeTile {
                    objectName: "shareButton"
                    label: qsTr("Share test text")
                    tileColour: "#e8dcc2"
                    onTapped: {
                        shareStatus.text = qsTr("Share sheet open…")
                        root.bridge.shareText(root.spikeShareText)
                    }
                }

                Text {
                    id: shareStatus

                    objectName: "shareStatus"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    color: "#4a463e"
                    font.pixelSize: 15
                    text: root.bridge.shareAvailable ? qsTr("Share: not tried yet") : qsTr("Share: unavailable here")
                }
            }
        }
    }

    // Prepare the impact generator ahead of the first press, and again whenever the app becomes active.
    Component.onCompleted: root.bridge.setBoardVisible(true)

    Connections {
        target: Qt.application

        function onStateChanged() {
            root.bridge.setApplicationActive(Qt.application.state === Qt.ApplicationActive)
        }
    }

    Connections {
        target: root.bridge

        function onShareFinished(outcome, activityType) {
            shareStatus.text = qsTr("Share: %1").arg(outcome) + (activityType.length > 0 ? "\n" + activityType : "")
        }
    }

    component SpikeTile: Rectangle {
        id: tile

        required property string label
        property color tileColour: "#e0e0e0"

        signal pressedDown
        signal tapped

        width: parent ? parent.width : 0
        height: 88
        radius: 12
        color: area.pressed ? Qt.darker(tileColour, 1.15) : tileColour

        Text {
            anchors.centerIn: parent
            color: "#1f1d19"
            font.pixelSize: 18
            text: tile.label
        }

        MouseArea {
            id: area

            anchors.fill: parent
            // The impact fires on touch-down, never on release (spikes.md).
            onPressed: tile.pressedDown()
            onClicked: tile.tapped()
        }
    }
}
