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
//   Move earlier / later     moves the type one tile along the board (`moveBoardTile`). It is the way to reorder that
//                            needs no drag, and the one a screen reader uses. A type that is not on the board has none.
//
// The colour of a type does not change when it moves (it follows from the type's place in the master list). A refusal
// of the view model, or a save that failed, is shown in a banner at the top of the list. Every edit is one save.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left and "Done" at the top right.
SheetHost {
    id: root

    required property var settings

    // The last refusal of a move, or the last failed save; empty for none.
    property string problem: ""

    signal closeRequested

    Connections {
        function onWriteFailed(operation: string, message: string): void {
            root.problem = message;
        }

        target: root.settings
    }

    EditorPage {
        anchors.fill: parent
        title: qsTr("Error types")
        leadingText: qsTr("Back")
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

            Column {
                id: column

                x: Theme.space.screenMargin
                width: flick.width - 2 * Theme.space.screenMargin
                spacing: Theme.space.sectionGap

                Banner {
                    objectName: "problemBanner"
                    width: parent.width
                    visible: root.problem.length > 0
                    tone: Banner.Warning
                    title: qsTr("That did not work")
                    message: root.problem
                    onCloseRequested: root.problem = ""
                }

                GroupedList {
                    width: parent.width

                    EditorRow {
                        objectName: "addRow"
                        action: true
                        text: qsTr("Add an error type")
                        onActivated: editSheet.openForNew()
                    }
                }

                GroupedList {
                    objectName: "boardGroup"
                    width: parent.width
                    visible: boardRepeater.count > 0
                    title: qsTr("On your board and in Other · %1").arg(boardRepeater.count)

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
                            detail: qsTr("Board tile %1").arg(boardPosition)
                            swatchSlot: colourSlot
                            reorderable: true
                            canMoveEarlier: boardPosition > 1
                            canMoveLater: boardPosition < Theme.tileSlots
                            onActivated: editSheet.openFor(typeId, name, colourSlot)
                            onMoveEarlier: root.problem = root.settings.moveBoardTile(boardPosition, boardPosition - 1)
                            onMoveLater: root.problem = root.settings.moveBoardTile(boardPosition, boardPosition + 1)
                        }
                    }
                }

                GroupedList {
                    objectName: "otherGroup"
                    width: parent.width
                    visible: otherRepeater.count > 0
                    title: qsTr("Only in Other · %1").arg(otherRepeater.count)

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
                            detail: qsTr("Not on the board")
                            swatchSlot: colourSlot
                            onActivated: editSheet.openFor(typeId, name, colourSlot)
                        }
                    }
                }

                Text {
                    x: Theme.space.tileGap / 2
                    width: parent.width - 2 * x
                    color: Theme.colour.textSecondary
                    font: Theme.font.caption.font
                    text: qsTr("Use the arrows to move a type to another tile. The first %1 types fill the board. Each type keeps its colour.").arg(Theme.tileSlots)
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
