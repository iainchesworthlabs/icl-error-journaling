// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The page that changes the facilitator (PRF-01): a name field and Save. The name heads every log started after the
// change; the log that is open keeps the name it started with, and the page says so. A refusal of the view model (an
// empty name, a name over 200 characters) is shown under the field, which then has a red border, and Save is dimmed
// until the name is edited.
//
// Required: `settings`, the SettingsModel. `closeRequested` is the back button and the end of a good Save.
EditorPage {
    id: root

    required property var settings

    property string draftName: root.settings.facilitator
    property string refusal: ""

    signal closeRequested

    function save(): void {
        if (root.refusal.length > 0)
            return;
        const message = root.settings.setFacilitator(root.draftName);
        if (message.length === 0)
            root.closeRequested();
        else
            root.refusal = message;
    }

    title: qsTr("Facilitator")
    leadingText: qsTr("Back")
    leadingAccessibleName: qsTr("Back to Settings")
    trailingText: qsTr("Save")
    trailingEnabled: root.refusal.length === 0
    onLeadingTriggered: root.closeRequested()
    onTrailingTriggered: root.save()

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

            FormField {
                objectName: "facilitatorField"
                label: qsTr("Facilitator")
                text: root.draftName
                hint: qsTr("The name at the head of every log you share. It applies from the next log; the open log keeps the name it started with.")
                errorText: root.refusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftName = text;
                    root.refusal = "";
                }
                onAccepted: root.save()
            }
        }
    }
}
