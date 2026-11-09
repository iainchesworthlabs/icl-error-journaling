// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The sheet that chooses the error type for a tile of the board (Stage 4, package 4.12; PRF-05; BoardTypePicker): "Add
// error type" for an empty tile and "Change error type" for one with a type. At the top, "New error type…" asks for a new
// type for this position (`newTypeRequested`): the screen opens the name sheet, and the type is made and put on the tile in
// one change, so nobody has to visit the Error types list first. Under it the sheet lists every error type that is not
// retired, with the board position it has now, and the type of the tile is ticked. A choice
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

    // "New error type…" was chosen for the tile at `position`; the sheet has closed.
    signal newTypeRequested(int position)

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
        title: root.currentTypeId.length > 0 ? qsTr("Change error type") : qsTr("Add error type")
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

            Text {
                objectName: "settingsPickerCaption"
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.footnote.font
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.font.footnote.linePitch
                text: qsTr("For position %1 of %2. A type already on the board swaps places with it.").arg(root.position).arg(Theme.size.boardColumns * Theme.size.boardRows - 1)
                wrapMode: Text.WordWrap
            }

            GroupedList {
                width: parent.width
                surface: Theme.colour.surfaceRaised

                ListRow {
                    objectName: "settingsPickerNewType"
                    text: qsTr("New error type…")
                    accessibleName: qsTr("New error type for position %1").arg(root.position)
                    leadingIcon: "plus"
                    leadingIconStroke: Theme.border.hairline * 2.2
                    labelColour: Theme.colour.primaryText
                    showChevron: true
                    onActivated: {
                        const position = root.position;
                        root.close();
                        root.newTypeRequested(position);
                    }
                }
            }

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
                visible: typeRows.count > 0
                title: qsTr("Error types")
                surface: Theme.colour.surfaceRaised

                Repeater {
                    id: typeRows

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
