// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// What the Settings list shows until the saved setup has been read (`settings.loading`): the shapes of the miniature board
// and of the list, in the neutral swatch colour, with no text and no type colour, so the first frame offers nothing that
// is not true yet. The shapes have the sizes of the real ones, so nothing moves when the setup arrives.
//
// This file is a part of SettingsScreen and is tested through it.
Item {
    id: root

    // The sizes of the tiles of MiniBoard, derived from the board's in the same way.
    readonly property real gap: Theme.space.tileGap * 0.75
    readonly property int gapCount: Theme.size.boardColumns - 1
    readonly property real tileWidth: (width - gapCount * gap) / Theme.size.boardColumns
    readonly property real tileHeight: Theme.size.tileHeight * 0.6
    readonly property int rowCount: 6

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: column.height

    Accessible.role: Accessible.StaticText
    Accessible.name: qsTr("Loading settings")

    Column {
        id: column

        width: parent.width
        spacing: Theme.space.sectionGap

        Grid {
            columns: Theme.size.boardColumns
            spacing: root.gap

            Repeater {
                model: Theme.size.boardColumns * Theme.size.boardRows

                delegate: Rectangle {
                    objectName: "settingsSkeletonTile"
                    width: root.tileWidth
                    height: root.tileHeight
                    radius: Theme.radius.field
                    color: Theme.colour.swatch
                    Accessible.ignored: true
                }
            }
        }

        Rectangle {
            width: parent.width
            height: root.rowCount * Theme.size.listRowHeight
            radius: Theme.radius.groupedList
            color: Theme.colour.surface
            Accessible.ignored: true

            Column {
                anchors.fill: parent

                Repeater {
                    model: root.rowCount

                    delegate: Item {
                        width: parent.width
                        height: Theme.size.listRowHeight
                        Accessible.ignored: true

                        Rectangle {
                            objectName: "settingsSkeletonBar"
                            x: Theme.space.rowPadding
                            y: (parent.height - height) / 2
                            width: parent.width * 0.4
                            height: Theme.space.sectionGap
                            radius: Theme.border.otherWidth * 4
                            color: Theme.colour.swatch
                            Accessible.ignored: true
                        }
                    }
                }
            }
        }
    }
}
