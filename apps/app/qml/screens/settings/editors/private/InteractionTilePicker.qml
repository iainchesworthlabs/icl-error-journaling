// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The sheet that chooses the interaction type for a tile of the miniature board (Stage 14, package I-7; the counterpart of
// SettingsTypePicker): "Add interaction type" for an empty tile and "Change interaction type" for one with a type. At the top,
// "New interaction type…" asks for a new type for this position (`newTypeRequested`): the screen opens its name sheet. Under it
// the sheet lists every interaction type that is not retired, with the board position it has now, and the type of the tile is
// ticked. A choice calls `settings.assignInteractionTile(position, typeId)`; if the model refuses, its words show at the top
// of the sheet and the sheet stays open, and if it accepts the sheet closes. The type that was on the board elsewhere swaps
// places with the tile's, and one that was not takes the tile and sends the tile's type off the board.
//
// This file is a part of InteractionTypes and is tested through it.
Sheet {
    id: root

    required property var settings
    // The position of the tile that is being given a type, 1 to 11, and the type it has now.
    property int position: 0
    property string currentTypeId: ""
    // The model's refusal of the last choice. Empty for none.
    property string message: ""

    // "New interaction type…" was chosen; the sheet has closed.
    signal newTypeRequested

    // Opens the sheet for the tile at `forPosition`.
    function openFor(forPosition: int, typeId: string): void {
        position = forPosition;
        currentTypeId = typeId;
        message = "";
        open();
    }

    accessibleName: qsTr("Choose an interaction type")
    detent: Sheet.Large

    NavBar {
        id: bar

        objectName: "tilePickerBar"
        width: parent.width
        title: root.currentTypeId.length > 0 ? qsTr("Change interaction type") : qsTr("Add interaction type")
        leadingText: qsTr("Cancel")
        onLeadingTriggered: root.close()
    }

    Flickable {
        id: flick

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
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.footnote.font
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.font.footnote.linePitch
                text: qsTr("For position %1 of %2. A type already on the board swaps places with it.").arg(root.position).arg(Theme.tileSlots)
                wrapMode: Text.WordWrap
            }

            GroupedList {
                width: parent.width
                surface: Theme.colour.surfaceRaised

                ListRow {
                    objectName: "tilePickerNewType"
                    text: qsTr("New interaction type…")
                    accessibleName: qsTr("New interaction type for position %1").arg(root.position)
                    leadingIcon: "plus"
                    leadingIconStroke: Theme.border.hairline * 2.2
                    labelColour: Theme.colour.primaryText
                    showChevron: true
                    onActivated: {
                        root.close();
                        root.newTypeRequested();
                    }
                }
            }

            Banner {
                objectName: "tilePickerMessage"
                width: parent.width
                visible: root.message.length > 0
                title: root.message
                tone: Banner.Error
                onCloseRequested: root.message = ""
            }

            GroupedList {
                width: parent.width
                visible: typeRows.count > 0
                title: qsTr("Interaction types")
                surface: Theme.colour.surfaceRaised

                Repeater {
                    id: typeRows

                    model: root.settings.interactionTypes

                    delegate: ListRow {
                        id: row

                        required property string typeId
                        required property string name
                        required property int boardPosition
                        required property bool onBoard

                        objectName: "tilePickerType"
                        text: row.name
                        value: row.onBoard ? qsTr("Position %1").arg(row.boardPosition) : qsTr("Not on the board")
                        checkable: true
                        checked: row.typeId === root.currentTypeId
                        onActivated: {
                            const answer = root.settings.assignInteractionTile(root.position, row.typeId);
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
