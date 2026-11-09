// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The locations editor of Settings (docs/design/screens/Locations.dc.html, PRF-03): a field and "Add location" at the
// top, then the locations in the order Today's details lists them.
//
//   a row                   opens a sheet to rename the location
//   the handle              hold and drag it up or down to move it by whole rows (`moveLocation`, with 0-based rows). The
//                           mock-up draws no arrows, so a screen reader moves it with the handle's "increase" and
//                           "decrease" actions
//   Remove                  retires the location: it leaves the list and the pickers, and past logs still name it
//
// A refusal of the view model (an empty or repeated name, a name over 200 characters, 500 locations) is shown under the
// field or in the sheet. A refusal of a move or a removal, or a save that failed, is a banner at the top of the list.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left and "Done" at the top right.
SheetHost {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // form column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

    required property var settings
    // The layer the shell presents sheets in, so that the sheet covers the tab bar. Null keeps it in this page's layer.
    property Item sheetLayer: null

    // The name in the field of the new location, and why the view model refused it.
    property string newName: ""
    property string newRefusal: ""
    // The last refusal of a move or a removal, or the last failed save; empty for none.
    property string problem: ""

    signal closeRequested

    function addLocation(): void {
        const message = root.settings.addLocation(root.newName);
        if (message.length === 0)
            root.newName = "";
        else
            root.newRefusal = message;
    }

    // The sheet that renames a location.
    component LocationSheet: Sheet {
        id: sheet

        required property var settings
        property string locationId: ""
        property string draftName: ""
        property string refusal: ""

        function openFor(id: string, name: string): void {
            sheet.locationId = id;
            sheet.draftName = name;
            sheet.refusal = "";
            sheet.open();
            renameField.focusField();
        }

        function save(): void {
            if (sheet.refusal.length > 0)
                return;
            const message = sheet.settings.renameLocation(sheet.locationId, sheet.draftName);
            if (message.length === 0)
                sheet.close();
            else
                sheet.refusal = message;
        }

        accessibleName: qsTr("Location")
        detent: Sheet.Large

        NavBar {
            id: bar

            // The bar sits 8 pt higher than `inSheet` puts it: the sheet's body already starts 21 pt down, as the mock-ups'
            // grabber and its 8 pt do.
            y: -Theme.space.tileGap
            width: parent.width
            inSheet: true
            title: qsTr("Location")
            leadingText: qsTr("Cancel")
            trailingText: qsTr("Save")
            trailingEnabled: sheet.refusal.length === 0
            onLeadingTriggered: sheet.close()
            onTrailingTriggered: sheet.save()
        }

        Flickable {
            id: sheetFlick

            y: bar.y + bar.height
            width: parent.width
            height: parent.height - y
            clip: true
            contentWidth: width
            contentHeight: renameField.height + Theme.space.screenMargin
            boundsBehavior: Flickable.StopAtBounds

            FormField {
                id: renameField

                objectName: "renameField"
                x: Theme.space.screenMargin
                width: sheetFlick.width - 2 * Theme.space.screenMargin
                compact: true
                surface: Theme.colour.surfaceRaised
                label: qsTr("Name")
                text: sheet.draftName
                errorText: sheet.refusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    sheet.draftName = text;
                    sheet.refusal = "";
                }
                onAccepted: sheet.save()
            }
        }
    }

    Binding {
        target: renameSheet
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
    }

    Connections {
        function onWriteFailed(operation: string, message: string): void {
            root.problem = message;
        }

        target: root.settings
    }

    EditorPage {
        anchors.fill: parent
        title: qsTr("Locations")
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

            // The gaps are the mock-up's own (Locations: 8 pt between the blocks, 16 above the button, 28 below it), so
            // they are set item by item and the column adds none.
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

                FormField {
                    objectName: "newLocationField"
                    compact: true
                    border: true
                    label: qsTr("New location")
                    text: root.newName
                    errorText: root.newRefusal
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onTextEdited: text => {
                        root.newName = text;
                        root.newRefusal = "";
                    }
                    onAccepted: root.addLocation()
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap * 2
                }

                PrimaryButton {
                    objectName: "addButton"
                    width: parent.width
                    height: Theme.size.smallButtonHeight
                    text: qsTr("Add location")
                    onClicked: root.addLocation()
                }

                Item {
                    width: parent.width
                    height: Theme.space.sectionGap * 7 / 3
                }

                GroupedList {
                    objectName: "locationGroup"
                    width: parent.width
                    visible: repeater.count > 0
                    title: qsTr("Locations · %1").arg(repeater.count)
                    footer: qsTr("The Today’s details list shows these in this order, then “Other…”. Removing one doesn’t change past logs.")

                    Repeater {
                        id: repeater

                        model: root.settings.locations

                        delegate: EditorRow {
                            required property int index
                            required property string locationId
                            required property string name

                            objectName: "locationRow"
                            text: name
                            canMoveEarlier: index > 0
                            canMoveLater: index < repeater.count - 1
                            draggable: true
                            removable: true
                            onActivated: renameSheet.openFor(locationId, name)
                            onMoveEarlier: root.problem = root.settings.moveLocation(index, index - 1)
                            onMoveLater: root.problem = root.settings.moveLocation(index, index + 1)
                            onRemoveRequested: root.problem = root.settings.removeLocation(locationId)
                            onDragged: rows => {
                                if (rows !== 0)
                                    root.problem = root.settings.moveLocation(index, Math.max(0, Math.min(repeater.count - 1, index + rows)));
                            }
                        }
                    }
                }
            }
        }
    }

    sheets: [
        LocationSheet {
            id: renameSheet

            settings: root.settings
        }
    ]
}
