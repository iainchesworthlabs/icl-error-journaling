// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The sheet that chooses the error type for a tile of the board (Stage 4, package 4.12; PRF-05): "Replace type". It lists
// every error type that is not retired, with the board position it has now, and the type of the tile is ticked. A choice
// calls `settings.assignBoardTile(position, typeId)`; if the model refuses, its words show at the top of the sheet and the
// sheet stays open, and if it accepts the sheet closes. The type that was on the board elsewhere swaps places with the
// tile's, and one that was not takes the tile and sends the tile's type off the board: the model does that, and the board
// shows it.
//
// This file is a part of SettingsScreen and is tested through it.
Sheet {
    id: root

    required property var settings
    // The position of the tile that is being given a type, 1 to 11, and the type it has now.
    property int position: 0
    property string currentTypeId: ""
    // The model's refusal of the last choice. Empty for none.
    property string message: ""

    // Opens the sheet for the tile at `forPosition`.
    function openFor(forPosition: int, typeId: string): void {
        position = forPosition;
        currentTypeId = typeId;
        message = "";
        open();
    }

    accessibleName: qsTr("Choose an error type")
    detent: Sheet.Large

    NavBar {
        id: bar

        objectName: "settingsPickerBar"
        width: parent.width
        title: qsTr("Replace type")
        leadingText: qsTr("Cancel")
        onLeadingTriggered: root.close()
    }

    Flickable {
        id: flick

        objectName: "settingsPickerFlickable"
        y: bar.height
        width: parent.width
        height: parent.height - y
        contentWidth: width
        contentHeight: list.height
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: list

            x: Theme.space.screenMargin
            width: flick.width - 2 * x
            spacing: Theme.space.sectionGap

            Banner {
                objectName: "settingsPickerMessage"
                width: parent.width
                visible: root.message.length > 0
                title: root.message
                tone: Banner.Error
                onCloseRequested: root.message = ""
            }

            GroupedList {
                width: parent.width
                surface: Theme.colour.surfaceRaised

                Repeater {
                    model: root.settings.errorTypes

                    delegate: ListRow {
                        id: row

                        required property string typeId
                        required property string name
                        required property int boardPosition
                        required property bool onBoard

                        objectName: "settingsPickerType_" + row.typeId
                        text: row.name
                        value: row.onBoard ? qsTr("Position %1").arg(row.boardPosition) : qsTr("Not on the board")
                        checkable: true
                        checked: row.typeId === root.currentTypeId
                        onActivated: {
                            const answer = root.settings.assignBoardTile(root.position, row.typeId);
                            root.message = answer;
                            if (answer.length === 0)
                                root.close();
                        }
                    }
                }
            }
        }
    }
}
