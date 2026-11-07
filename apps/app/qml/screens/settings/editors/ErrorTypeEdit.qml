// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The sheet that adds, renames or retires an error type (docs/design/screens/ErrorTypeEdit.dc.html). The Error types
// screen declares one and opens it with `openForNew()` or `openFor(typeId, name, colourSlot)`.
//
//   Name     a field; Save asks the view model (`addErrorType` or `renameErrorType`). A refusal (an empty name, a name that
//            is already used by another type, ignoring case, a name over 200 characters, 500 types) is shown under the
//            field, which then has a red border, and Save is dimmed until the person edits the name.
//   Colour   the swatch the type keeps. It is not chosen: the colour follows from the type's place in the master list.
//   Retire   "Retire this error type" (`retireErrorType`). The type leaves the board and Other and stays on the entries
//            already logged. Not shown for a new type.
//
// The sheet closes itself when the view model accepts the change. The list behind it changes when the save has finished.
Sheet {
    id: root

    required property var settings
    // The type being edited; empty for a new one.
    property string typeId: ""
    property string typeName: ""
    // The colour slot of the type (1 to 11); 0 for a new type, which has none yet.
    property int colourSlot: 0

    // What the field shows, and why the view model refused it.
    property string draftName: ""
    property string refusal: ""
    property string retireRefusal: ""

    readonly property bool adding: typeId.length === 0

    function openForNew(): void {
        root.typeId = "";
        root.typeName = "";
        root.colourSlot = 0;
        root.start();
    }

    function openFor(id: string, name: string, slot: int): void {
        root.typeId = id;
        root.typeName = name;
        root.colourSlot = slot;
        root.start();
    }

    function start(): void {
        root.draftName = root.typeName;
        root.refusal = "";
        root.retireRefusal = "";
        root.open();
        nameField.focusField();
    }

    function save(): void {
        if (root.refusal.length > 0)
            return;
        const message = root.adding ? root.settings.addErrorType(root.draftName) : root.settings.renameErrorType(root.typeId, root.draftName);
        if (message.length === 0)
            root.close();
        else
            root.refusal = message;
    }

    function retire(): void {
        const message = root.settings.retireErrorType(root.typeId);
        if (message.length === 0)
            root.close();
        else
            root.retireRefusal = message;
    }

    accessibleName: root.adding ? qsTr("New error type") : qsTr("Error type")
    detent: Sheet.Large

    NavBar {
        id: bar

        width: parent.width
        title: root.adding ? qsTr("New error type") : qsTr("Error type")
        leadingText: qsTr("Cancel")
        trailingText: qsTr("Save")
        trailingEnabled: root.refusal.length === 0
        onLeadingTriggered: root.close()
        onTrailingTriggered: root.save()
    }

    Flickable {
        id: flick

        y: bar.height
        width: parent.width
        height: parent.height - bar.height
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.sectionGap

            FormField {
                id: nameField

                objectName: "nameField"
                label: qsTr("Name")
                text: root.draftName
                errorText: root.refusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftName = text;
                    root.refusal = "";
                }
                onAccepted: root.save()
            }

            GroupedList {
                width: parent.width
                visible: !root.adding
                title: qsTr("Colour")
                surface: Theme.colour.surfaceRaised

                Item {
                    id: colourRow

                    width: parent.width
                    height: Math.max(Theme.size.listRowHeight, colourContent.height + Theme.space.sectionGap)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Colour. It stays with the type when the board is reordered.")

                    Row {
                        id: colourContent

                        x: Theme.space.rowPadding
                        y: (parent.height - height) / 2
                        width: parent.width - 2 * Theme.space.rowPadding
                        spacing: Theme.space.sectionGap

                        Rectangle {
                            objectName: "editSwatch"
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.size.badge * 0.75
                            height: width
                            radius: Theme.radius.field / 3
                            color: Theme.tileColour(root.colourSlot)
                            border.width: Theme.border.hairline
                            border.color: Theme.colour.border
                            Accessible.ignored: true
                        }

                        Text {
                            width: parent.width - Theme.size.badge * 0.75 - Theme.space.sectionGap
                            color: Theme.colour.textSecondary
                            font: Theme.font.caption.font
                            text: qsTr("Stays with the type when the board is reordered")
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                }
            }

            GroupedList {
                width: parent.width
                visible: !root.adding
                surface: Theme.colour.surfaceRaised
                footer: root.retireRefusal.length > 0 ? root.retireRefusal : qsTr("A retired type disappears from the board and from Other. It stays on entries already logged.")

                ListRow {
                    objectName: "retireRow"
                    text: qsTr("Retire this error type")
                    destructive: true
                    onActivated: root.retire()
                }
            }
        }
    }
}
