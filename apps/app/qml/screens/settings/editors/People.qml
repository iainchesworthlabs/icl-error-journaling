// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The People screen of Settings (Stage 14, package I-7; docs/plan/stage-14.md, 4.5): the people the Interactions board can
// name. The names stay on the device, in a list of their own, and an entry refers to a person by id.
//
//   New person    a name, and optionally the location the person belongs to. "Everyone" in the picker ticks the people of the
//                 location in force, so a person with a location is found there first.
//   a row         opens a sheet: rename (the history follows), retire (the person leaves the picker and stays on past entries;
//                 "Bring back" undoes it) and Forget.
//   Forget        asks first: "Forget Alex Sample? Entries stay but show someone." It removes the name from the app's files; the
//                 entries keep their place and show "someone".
//
// A refusal of the view model (an empty or repeated name, a name over 60 characters) is shown under the field or in the sheet;
// a save that failed is a banner at the top of the list.
//
// Required: `settings` (for the locations) and `people`, the PeopleAdminModel. `closeRequested` is "Settings" at the top left
// and "Done" at the top right.
SheetHost {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the form
    // column of a larger window.
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

    required property var settings
    required property var people
    // The layer the shell presents sheets in, so that the sheet covers the tab bar. Null keeps it in this page's layer.
    property Item sheetLayer: null

    // The name in the field of the new person, why the view model refused it, and the location chosen for them ("" for none).
    property string newName: ""
    property string newRefusal: ""
    property string newLocation: ""
    // The last failed save; empty for none.
    property string problem: ""
    // The person Forget was asked for.
    property string forgetId: ""
    property string forgetName: ""

    signal closeRequested
    signal helpRequested(string topic)

    function addPerson(): void {
        const message = root.people.addPerson(root.newName, root.newLocation);
        if (message.length === 0) {
            root.newName = "";
            root.newLocation = "";
        } else {
            root.newRefusal = message;
        }
    }

    // The name of a location that is not retired, or "".
    function locationName(id: string): string {
        const model = root.settings.locations;
        for (let row = 0; row < model.rowCount(); ++row) {
            const at = model.index(row, 0);
            if (model.data(at, Qt.UserRole + 1) === id)
                return model.data(at, Qt.UserRole + 2);
        }
        return "";
    }

    function detailOf(retired: bool, locationId: string): string {
        const place = locationId.length > 0 ? root.locationName(locationId) : "";
        if (retired)
            return place.length > 0 ? qsTr("Retired · %1").arg(place) : qsTr("Retired");
        return place;
    }

    // The sheet of one person.
    component PersonSheet: Sheet {
        id: sheet

        required property var people
        property string personId: ""
        property string draftName: ""
        property bool retired: false
        property string refusal: ""

        signal forgetRequested(string personId, string name)

        function openFor(id: string, name: string, isRetired: bool): void {
            sheet.personId = id;
            sheet.draftName = name;
            sheet.retired = isRetired;
            sheet.refusal = "";
            sheet.open();
        }

        function save(): void {
            if (sheet.refusal.length > 0)
                return;
            const message = sheet.people.renamePerson(sheet.personId, sheet.draftName);
            if (message.length === 0)
                sheet.close();
            else
                sheet.refusal = message;
        }

        function toggleRetired(): void {
            const message = sheet.people.setRetired(sheet.personId, !sheet.retired);
            if (message.length === 0)
                sheet.close();
            else
                sheet.refusal = message;
        }

        accessibleName: qsTr("Person")
        detent: Sheet.Large

        NavBar {
            id: bar

            y: -Theme.space.tileGap
            width: parent.width
            inSheet: true
            title: qsTr("Person")
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
            contentHeight: sheetColumn.height + Theme.space.screenMargin
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: sheetColumn

                x: Theme.space.screenMargin
                width: sheetFlick.width - 2 * Theme.space.screenMargin
                spacing: Theme.space.sectionGap

                FormField {
                    id: renameField

                    objectName: "personNameField"
                    width: parent.width
                    compact: true
                    surface: Theme.colour.surfaceRaised
                    label: qsTr("Name")
                    text: sheet.draftName
                    errorText: sheet.refusal
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onTextEdited: text => {
                        sheet.draftName = text;
                        sheet.refusal = sheet.people.validateName(text, sheet.personId);
                    }
                    onAccepted: sheet.save()
                }

                GroupedList {
                    width: parent.width
                    footer: sheet.retired ? qsTr("A retired person is not in the picker. Entries that name them still show the name.") : qsTr("A retired person leaves the picker. Entries that name them still show the name.")

                    ListRow {
                        objectName: "personRetire"
                        text: sheet.retired ? qsTr("Bring back") : qsTr("Retire")
                        minimumHeight: Theme.size.rowHeightCompact
                        onActivated: sheet.toggleRetired()
                    }

                    ListRow {
                        objectName: "personForget"
                        text: qsTr("Forget")
                        destructive: true
                        minimumHeight: Theme.size.rowHeightCompact
                        onActivated: {
                            const id = sheet.personId;
                            const name = sheet.draftName;
                            sheet.close();
                            sheet.forgetRequested(id, name);
                        }
                    }
                }
            }
        }
    }

    Binding {
        target: personSheet
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
    }

    Binding {
        target: forgetAlert
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
        restoreMode: Binding.RestoreNone
    }

    Connections {
        function onFailed(message: string): void {
            root.problem = message;
        }

        target: root.people
    }

    EditorPage {
        anchors.fill: parent
        title: qsTr("People")
        leadingText: qsTr("Settings")
        leadingAccessibleName: qsTr("Back to Settings")
        trailingText: qsTr("Done")
        helpName: qsTr("Help with people")
        onHelpRequested: root.helpRequested("people")
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
                    objectName: "peopleIntro"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.colour.textSecondary
                    font: Theme.font.footnote.font
                    text: !root.people.available ? qsTr("People are not available right now.") : qsTr("The people you can name on an interaction such as “Spoke with [person]”. Names stay on this device; a shared log shows them as you choose in Settings.")
                }

                Item {
                    width: parent.width
                    height: Theme.space.sectionGap
                }

                FormField {
                    objectName: "newPersonField"
                    width: parent.width
                    compact: true
                    border: true
                    enabled: root.people.available
                    label: qsTr("New person")
                    text: root.newName
                    errorText: root.newRefusal
                    inputMethodHints: Qt.ImhNoPredictiveText
                    onTextEdited: text => {
                        root.newName = text;
                        root.newRefusal = "";
                    }
                    onAccepted: root.addPerson()
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap * 2
                    visible: locationChoice.visible
                }

                GroupedList {
                    id: locationChoice

                    objectName: "newPersonLocations"
                    width: parent.width
                    visible: locationRepeater.count > 0
                    title: qsTr("Location of the new person")
                    footer: qsTr("Everyone in the picker means the people of the location in force.")

                    ListRow {
                        objectName: "newPersonNoLocation"
                        text: qsTr("No location")
                        minimumHeight: Theme.size.rowHeightCompact
                        checkable: true
                        checked: root.newLocation.length === 0
                        onActivated: root.newLocation = ""
                    }

                    Repeater {
                        id: locationRepeater

                        model: root.settings.locations

                        delegate: ListRow {
                            required property string locationId
                            required property string name

                            objectName: "newPersonLocation"
                            text: name
                            minimumHeight: Theme.size.rowHeightCompact
                            checkable: true
                            checked: root.newLocation === locationId
                            onActivated: root.newLocation = locationId
                        }
                    }
                }

                Item {
                    width: parent.width
                    height: Theme.space.tileGap * 2
                }

                PrimaryButton {
                    objectName: "addPersonButton"
                    width: parent.width
                    height: Theme.size.smallButtonHeight
                    enabled: root.people.available
                    text: qsTr("Add person")
                    onClicked: root.addPerson()
                }

                Item {
                    width: parent.width
                    height: Theme.space.sectionGap * 7 / 3
                }

                GroupedList {
                    objectName: "peopleGroup"
                    width: parent.width
                    visible: peopleRepeater.count > 0
                    title: qsTr("People · %1").arg(peopleRepeater.count)
                    footer: qsTr("Forget removes a name for good. The entries stay and show “someone”.")

                    Repeater {
                        id: peopleRepeater

                        model: root.people

                        delegate: EditorRow {
                            required property string personId
                            required property string name
                            required property bool retired
                            required property string locationId

                            objectName: "personRow"
                            text: name
                            detail: root.detailOf(retired, locationId)
                            chevron: true
                            onActivated: personSheet.openFor(personId, name, retired)
                        }
                    }
                }
            }
        }
    }

    sheets: [
        PersonSheet {
            id: personSheet

            people: root.people
            onForgetRequested: (id, name) => {
                root.forgetId = id;
                root.forgetName = name;
                forgetAlert.open();
            }
        },
        Alert {
            id: forgetAlert

            objectName: "forgetAlert"
            title: qsTr("Forget %1?").arg(root.forgetName)
            message: qsTr("Entries stay but show “someone”. This cannot be undone.")
            icon: Alert.Trash
            destructive: true
            confirmText: qsTr("Forget")
            cancelText: qsTr("Cancel")
            onConfirmed: {
                const message = root.people.forgetPerson(root.forgetId);
                if (message.length > 0)
                    root.problem = message;
                root.forgetId = "";
            }
            onCancelled: root.forgetId = ""
        }
    ]
}
