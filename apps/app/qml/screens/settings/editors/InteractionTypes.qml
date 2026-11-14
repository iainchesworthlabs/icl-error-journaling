// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import ErrorJournalingApp

// The interaction types editor of Settings (Stage 14, 4.5; PRF-04 for the second board). It is the error types editor with
// the name changed, and the line of each type under its name:
//
//   Add an interaction type   opens the sheet with an empty name
//   a row                     opens the sheet to edit the type, or retire it
//   the handle, the arrows    move the type along the board (`moveInteractionTile`)
//
// A new type goes onto the first empty tile of the board in the same save. A refusal, or a save that failed, shows in a
// banner at the top of the list. Every edit is one save.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left and "Done" at the top right.
SheetHost {
    id: root

    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

    required property var settings
    property Item sheetLayer: null
    property string problem: ""

    // A tile that was asked to move, and where it should arrive: the selection goes with it once the model has moved it.
    property string followTypeId: ""
    property int followPosition: 0

    signal closeRequested
    signal helpRequested(string topic)

    // Opens the sheet for a new type: the empty board asks for it.
    function addType(): void {
        editSheet.openForNew();
    }

    Binding {
        target: editSheet
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
    }

    Binding {
        target: tilePicker
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
    }

    // Asks for a move, and remembers which tile it was if it is the selected one.
    function moveTile(fromPosition: int, toPosition: int): void {
        if (miniBoard.selectedPosition === fromPosition) {
            root.followTypeId = miniBoard.tileItem(fromPosition - 1).typeId;
            root.followPosition = toPosition;
        }
        root.problem = root.settings.moveInteractionTile(fromPosition, toPosition);
        if (root.problem.length > 0)
            root.followTypeId = "";
    }

    // The board changed: if the tile that was asked to move has arrived, the selection moves to it.
    function followSelected(): void {
        if (root.followTypeId.length === 0)
            return;
        const arrived = miniBoard.tileItem(root.followPosition - 1);
        if (arrived && arrived.typeId === root.followTypeId) {
            miniBoard.selectedPosition = root.followPosition;
            root.followTypeId = "";
        }
    }

    Connections {
        target: root.settings.interactionLayout

        // After the board has taken the change, so the tile it arrives on has its new type.
        function onDataChanged(): void {
            Qt.callLater(root.followSelected);
        }
    }

    Connections {
        function onWriteFailed(operation: string, message: string): void {
            root.problem = message;
        }

        target: root.settings
    }

    EditorPage {
        anchors.fill: parent
        title: qsTr("Interaction types")
        leadingText: qsTr("Settings")
        leadingAccessibleName: qsTr("Back to Settings")
        trailingText: qsTr("Done")
        helpName: qsTr("Help with interaction types")
        onHelpRequested: root.helpRequested("interactionTypes")
        onLeadingTriggered: root.closeRequested()
        onTrailingTriggered: root.closeRequested()

        Flickable {
            id: flick

            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: column.height + Theme.space.screenMargin
            boundsBehavior: Flickable.StopAtBounds
            interactive: !miniBoard.dragging

            Column {
                id: column

                x: root.side
                width: flick.width - 2 * root.side
                spacing: 0

                Banner {
                    objectName: "problemBanner"
                    width: parent.width
                    visible: root.problem.length > 0
                    tone: Banner.Warning
                    title: qsTr("That did not work")
                    message: root.problem
                    onCloseRequested: root.problem = ""
                }

                Item {
                    width: parent.width
                    height: root.problem.length > 0 ? Theme.space.tileGap : 0
                }

                Text {
                    x: Theme.space.tileGap / 2
                    width: parent.width - 2 * x
                    color: Theme.colour.textSecondary
                    font: Theme.font.sectionLabel.font
                    text: qsTr("Your interaction board").toLocaleUpperCase()
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap
                }

                MiniBoard {
                    id: miniBoard

                    objectName: "interactionMiniBoard"
                    width: parent.width
                    model: root.settings.interactionLayout
                    addText: qsTr("Add interaction type")
                    changeText: qsTr("Change interaction type")
                    onMoveRequested: (fromPosition, toPosition) => root.moveTile(fromPosition, toPosition)
                    onReplaceRequested: position => tilePicker.openFor(position, miniBoard.tileItem(position - 1).typeId)
                    onAddRequested: position => tilePicker.openFor(position, "")
                }

                Item {
                    width: parent.width
                    height: Theme.space.sectionGap
                }

                Text {
                    objectName: "introText"
                    x: Theme.space.tileGap / 2
                    width: parent.width - 2 * x
                    wrapMode: Text.WordWrap
                    color: Theme.colour.textSecondary
                    font: Theme.font.bodySmall.font
                    text: boardRepeater.count + otherRepeater.count === 0 ? qsTr("These are the tiles of the Interactions board: what you did around the unit. Add one, for example “Spoke with [person]”. A type with [person] asks who after you log it.") : qsTr("Types with a position are on the Interactions board. The rest are in Other. A type with [person] asks who after you log it.")
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap
                }

                GroupedList {
                    width: parent.width

                    EditorRow {
                        objectName: "addRow"
                        action: true
                        minimumHeight: Theme.size.listRowHeight
                        text: qsTr("Add an interaction type")
                        onActivated: editSheet.openForNew()
                    }
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap * 2
                }

                GroupedList {
                    objectName: "boardGroup"
                    width: parent.width
                    visible: boardRepeater.count > 0
                    title: qsTr("On the board · %1").arg(boardRepeater.count)

                    Repeater {
                        id: boardRepeater

                        model: SortFilterProxyModel {
                            sourceModel: root.settings.interactionTypes
                            filters: ValueFilter {
                                roleName: "onBoard"
                                value: true
                            }
                            sorters: RoleSorter {
                                roleName: "boardPosition"
                            }
                        }

                        delegate: EditorRow {
                            required property string typeId
                            required property string name
                            required property string line
                            required property int boardPosition

                            objectName: "typeRow"
                            text: name
                            detail: line
                            accessibleName: qsTr("%1, board tile %2").arg(name).arg(boardPosition)
                            reorderable: true
                            draggable: true
                            canMoveEarlier: boardPosition > 1
                            canMoveLater: boardPosition < Theme.tileSlots
                            onActivated: editSheet.openFor(typeId, name, line)
                            onMoveEarlier: root.problem = root.settings.moveInteractionTile(boardPosition, boardPosition - 1)
                            onMoveLater: root.problem = root.settings.moveInteractionTile(boardPosition, boardPosition + 1)
                            onDragged: rows => {
                                if (rows !== 0)
                                    root.problem = root.settings.moveInteractionTile(boardPosition, Math.max(1, Math.min(Theme.tileSlots, boardPosition + rows)));
                            }
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: otherGroup.visible ? Theme.space.tileGap * 2 : 0
                }

                GroupedList {
                    id: otherGroup

                    objectName: "otherGroup"
                    width: parent.width
                    visible: otherRepeater.count > 0
                    title: qsTr("Not on the board · %1").arg(otherRepeater.count)

                    Repeater {
                        id: otherRepeater

                        model: SortFilterProxyModel {
                            sourceModel: root.settings.interactionTypes
                            filters: ValueFilter {
                                roleName: "onBoard"
                                value: false
                            }
                        }

                        delegate: EditorRow {
                            required property string typeId
                            required property string name
                            required property string line

                            objectName: "typeRow"
                            text: name
                            detail: line
                            accessibleName: qsTr("%1, not on the board").arg(name)
                            onActivated: editSheet.openFor(typeId, name, line)
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap * 5 / 4
                }

                Text {
                    x: Theme.space.tileGap / 2
                    width: parent.width - 2 * x
                    color: Theme.colour.textSecondary
                    font: Theme.font.footnote.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.footnote.linePitch
                    text: qsTr("Drag the handle, or use the arrows. The first %1 types fill the board in this order.").arg(Theme.tileSlots)
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    sheets: [
        InteractionTypeEdit {
            id: editSheet

            settings: root.settings
        },
        InteractionTilePicker {
            id: tilePicker

            settings: root.settings
            onNewTypeRequested: editSheet.openForNew()
        }
    ]
}
