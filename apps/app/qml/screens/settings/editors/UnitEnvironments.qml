// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The unit environments editor of Settings (docs/design/screens/UnitEnvironments.dc.html, PRF-11): a field and "Add
// environment" at the top, then the environments in the order Today's details offers them, after "None". It is the
// Locations editor for a list of short names.
//
//   a row                   opens a sheet to rename the environment
//   the handle              hold and drag it up or down to move it by whole rows (`moveUnitEnvironment`, with 0-based
//                           rows). A screen reader moves it with the handle's "increase" and "decrease" actions
//   Remove                  takes it out of the list and the picker; a log whose unit has it keeps it
//
// A name is lower-case letters, digits and dashes (upper case is lowered as it is typed), at most 20 characters, not
// starting or ending with a dash. A refusal of the view model (a name that is not allowed, a repeat, the last one removed,
// 500 environments) is shown under the field or in the sheet; a refusal of a move or a removal, or a save that failed, is a
// banner at the top of the list.
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

    // The name in the field of the new environment, and why the view model refused it.
    property string newName: ""
    property string newRefusal: ""
    // The last refusal of a move or a removal, or the last failed save; empty for none.
    property string problem: ""

    signal closeRequested

    function addEnvironment(): void {
        const message = root.settings.addUnitEnvironment(root.newName);
        if (message.length === 0)
            root.newName = "";
        else
            root.newRefusal = message;
    }

    // The sheet that renames an environment.
    component EnvironmentRenameSheet: Sheet {
        id: sheet

        required property var settings
        property string environmentName: ""
        property string draftName: ""
        property string refusal: ""

        function openFor(name: string): void {
            sheet.environmentName = name;
            sheet.draftName = name;
            sheet.refusal = "";
            sheet.open();
            renameField.focusField();
        }

        function save(): void {
            if (sheet.refusal.length > 0)
                return;
            const message = sheet.settings.renameUnitEnvironment(sheet.environmentName, sheet.draftName);
            if (message.length === 0)
                sheet.close();
            else
                sheet.refusal = message;
        }

        accessibleName: qsTr("Environment")
        detent: Sheet.Large

        NavBar {
            id: bar

            // The bar sits 8 pt higher than `inSheet` puts it: the sheet's body already starts 21 pt down, as the mock-ups'
            // grabber and its 8 pt do.
            y: -Theme.space.tileGap
            width: parent.width
            inSheet: true
            title: qsTr("Environment")
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
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhPreferLowercase
                onTextEdited: text => {
                    sheet.draftName = text.toLowerCase();
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
        title: qsTr("Unit environments")
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

            // The gaps are those of the Locations editor, so they are set item by item and the column adds none.
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
                    objectName: "newEnvironmentField"
                    compact: true
                    border: true
                    label: qsTr("New environment")
                    text: root.newName
                    placeholderText: qsTr("For example prod")
                    hint: qsTr("Lower-case letters, digits and dashes.")
                    errorText: root.newRefusal
                    inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhPreferLowercase
                    onTextEdited: text => {
                        root.newName = text.toLowerCase();
                        root.newRefusal = "";
                    }
                    onAccepted: root.addEnvironment()
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap * 2
                }

                PrimaryButton {
                    objectName: "addButton"
                    width: parent.width
                    height: Theme.size.smallButtonHeight
                    text: qsTr("Add environment")
                    onClicked: root.addEnvironment()
                }

                Item {
                    width: parent.width
                    height: Theme.space.sectionGap * 7 / 3
                }

                GroupedList {
                    objectName: "environmentGroup"
                    width: parent.width
                    visible: repeater.count > 0
                    title: qsTr("Environments · %1").arg(repeater.count)
                    footer: qsTr("Today’s details offers these in this order, after “None”. Removing one doesn’t change past logs.")

                    Repeater {
                        id: repeater

                        model: root.settings.unitEnvironments

                        delegate: EditorRow {
                            required property int index
                            required property string modelData

                            objectName: "environmentRow"
                            text: modelData
                            canMoveEarlier: index > 0
                            canMoveLater: index < repeater.count - 1
                            draggable: true
                            removable: true
                            onActivated: renameSheet.openFor(modelData)
                            onMoveEarlier: root.problem = root.settings.moveUnitEnvironment(index, index - 1)
                            onMoveLater: root.problem = root.settings.moveUnitEnvironment(index, index + 1)
                            onRemoveRequested: root.problem = root.settings.removeUnitEnvironment(modelData)
                            onDragged: rows => {
                                if (rows !== 0)
                                    root.problem = root.settings.moveUnitEnvironment(index, Math.max(0, Math.min(repeater.count - 1, index + rows)));
                            }
                        }
                    }
                }
            }
        }
    }

    sheets: [
        EnvironmentRenameSheet {
            id: renameSheet

            settings: root.settings
        }
    ]
}
