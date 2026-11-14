// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The Other tile of the Interactions board (Stage 14, package I-6): a short sheet that lists the interaction types that
// are not on the board. Choosing one logs it, as a tile does, and closes the sheet. The sheet holds no rule: the list is
// the board model's `otherTypes`, rows {typeId, label}.
Sheet {
    id: root

    // Rows {typeId, label}.
    property var types: []

    // A type was chosen: the screen logs it.
    signal chosen(string typeId)

    function begin(): void {
        open();
    }

    accessibleName: qsTr("Log another interaction")
    detent: Sheet.Medium
    contentUnderHomeIndicator: true

    NavBar {
        id: nav

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: qsTr("Log another interaction")
        inSheet: true
        leadingText: qsTr("Cancel")
        onLeadingTriggered: root.close()
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: nav.bottom
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin + root.contentBottomInset
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.sectionGap

            Text {
                objectName: "interactionOtherEmpty"
                width: parent.width
                visible: root.types.length === 0
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("Every interaction type is on the board.")
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            GroupedList {
                width: parent.width
                visible: root.types.length > 0
                surface: Theme.colour.surfaceRaised

                Repeater {
                    model: root.types

                    ListRow {
                        id: row

                        required property var modelData

                        objectName: "interactionOtherRow:" + row.modelData.typeId
                        text: row.modelData.label
                        minimumHeight: Theme.size.rowHeightForm
                        onActivated: {
                            root.chosen(row.modelData.typeId);
                            root.close();
                        }
                    }
                }
            }
        }
    }
}
