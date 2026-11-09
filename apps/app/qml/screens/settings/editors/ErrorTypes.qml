// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import ErrorJournalingApp

// The error types editor of Settings (docs/design/screens/ErrorTypes.dc.html, PRF-04). It lists the types that are not
// retired, the ones on the board first in the order of their tiles and then the ones that are only in Other, and offers:
//
//   Add an error type        opens the sheet with an empty name
//   a row                    opens the sheet to rename the type or retire it
//   the handle               hold and drag it up or down to move the type along the board (`moveBoardTile`); a screen
//                            reader gets the same from the handle's "increase" and "decrease" actions
//   Move earlier / later     the arrows of the mock-up: they move the type one tile along the board. A type that is not on
//                            the board has none.
//
// The colour of a type does not change when it moves (it follows from the type's place in the master list). A refusal
// of the view model, or a save that failed, is shown in a banner at the top of the list. Every edit is one save.
//
// A new type goes onto the first empty tile of the board in the same save (`addErrorType`); when it is in force an
// information banner at the top says where (ErrorTypesAdded): "Added to the board at position 5", or that the board was
// full and the type is in Other.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left and "Done" at the top right.
SheetHost {
    id: root

    required property var settings
    // The layer the shell presents sheets in, so that the sheet covers the tab bar as the mock-up has it. Null keeps the
    // sheet in this page's own layer.
    property Item sheetLayer: null

    // The last refusal of a move, or the last failed save; empty for none.
    property string problem: ""
    // The banner of the type that was added last: its title and line. Empty for none.
    property string noticeTitle: ""
    property string noticeText: ""

    signal closeRequested

    Binding {
        target: editSheet
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
    }

    Connections {
        function onWriteFailed(operation: string, message: string): void {
            root.problem = message;
            root.noticeTitle = "";
        }

        function onErrorTypeAdded(name: string, position: int): void {
            if (position > 0) {
                root.noticeTitle = qsTr("Added to the board at position %1").arg(position);
                root.noticeText = qsTr("%1 is on the first empty tile.").arg(name);
            } else {
                root.noticeTitle = qsTr("Added to Other");
                root.noticeText = qsTr("The board is full, so %1 is in Other. Tap a tile in Settings to put it on the board.").arg(name);
            }
            Accessible.announce(qsTr("%1. %2").arg(root.noticeTitle).arg(root.noticeText));
        }

        target: root.settings
    }

    EditorPage {
        anchors.fill: parent
        title: qsTr("Error types")
        leadingText: qsTr("Settings")
        leadingAccessibleName: qsTr("Back to Settings")
        trailingText: qsTr("Done")
        onLeadingTriggered: root.closeRequested()
        onTrailingTriggered: root.closeRequested()

        Flickable {
            id: flick

            anchors.fill: parent
            clip: true
            contentWidth: width
            contentHeight: column.height + Theme.space.screenMargin
            boundsBehavior: Flickable.StopAtBounds

            // The gaps are the mock-up's own (ErrorTypes: 8 pt between the blocks and 8 more above a heading), so they are
            // set item by item and the column adds none.
            Column {
                id: column

                x: Theme.space.screenMargin
                width: flick.width - 2 * Theme.space.screenMargin
                spacing: 0

                Banner {
                    objectName: "addedBanner"
                    width: parent.width
                    visible: root.noticeTitle.length > 0
                    tone: Banner.Information
                    title: root.noticeTitle
                    message: root.noticeText
                    onCloseRequested: root.noticeTitle = ""
                }

                Item {
                    width: parent.width
                    height: root.noticeTitle.length > 0 ? Theme.space.tileGap : 0
                }

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

                GroupedList {
                    width: parent.width

                    EditorRow {
                        objectName: "addRow"
                        action: true
                        minimumHeight: Theme.size.listRowHeight
                        text: qsTr("Add an error type")
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
                    title: qsTr("On your board · %1").arg(boardRepeater.count)

                    Repeater {
                        id: boardRepeater

                        model: SortFilterProxyModel {
                            sourceModel: root.settings.errorTypes
                            filters: ValueFilter {
                                roleName: "onBoard"
                                value: true
                            }
                            sorters: RoleSorter {
                                roleName: "boardPosition"
                            }
                        }

                        delegate: EditorRow {
                            id: boardRow

                            required property string typeId
                            required property string name
                            required property int colourSlot
                            required property int boardPosition

                            objectName: "typeRow"
                            text: name
                            // The place on the board is not drawn (the mock-up has one line a row), and a screen reader
                            // still hears it.
                            accessibleName: qsTr("%1, board tile %2").arg(name).arg(boardPosition)
                            swatchSlot: colourSlot
                            reorderable: true
                            draggable: true
                            canMoveEarlier: boardPosition > 1
                            canMoveLater: boardPosition < Theme.tileSlots
                            onActivated: editSheet.openFor(typeId, name, colourSlot)
                            onMoveEarlier: root.problem = root.settings.moveBoardTile(boardPosition, boardPosition - 1)
                            onMoveLater: root.problem = root.settings.moveBoardTile(boardPosition, boardPosition + 1)
                            onDragged: rows => {
                                if (rows !== 0)
                                    root.problem = root.settings.moveBoardTile(boardPosition, Math.max(1, Math.min(Theme.tileSlots, boardPosition + rows)));
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
                    title: qsTr("Not on your board · %1").arg(otherRepeater.count)

                    Repeater {
                        id: otherRepeater

                        model: SortFilterProxyModel {
                            sourceModel: root.settings.errorTypes
                            filters: ValueFilter {
                                roleName: "onBoard"
                                value: false
                            }
                        }

                        delegate: EditorRow {
                            required property string typeId
                            required property string name
                            required property int colourSlot

                            objectName: "typeRow"
                            text: name
                            accessibleName: qsTr("%1, not on the board").arg(name)
                            swatchSlot: colourSlot
                            onActivated: editSheet.openFor(typeId, name, colourSlot)
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
                    text: qsTr("Drag the handle, or use the arrows. The first %1 types fill the board in this order. Each type keeps its colour.").arg(Theme.tileSlots)
                    wrapMode: Text.WordWrap
                }
            }
        }
    }

    sheets: [
        ErrorTypeEdit {
            id: editSheet

            settings: root.settings
        }
    ]
}
