// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The page that edits one line of text of the setup (Stage 4, package 4.12; PRF-01, PRF-02): the facilitator or the unit
// prefix, by `kind`. Save asks the model once. If the model refuses, its words stay under the field, the old value stays
// where it was, and the page stays open; if it accepts, the page closes and the list shows the value when it has been
// saved.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings
    // "facilitator" or "unitPrefix".
    required property string kind

    readonly property bool isFacilitator: kind === "facilitator"
    // The value in force, which the draft starts from and is compared with.
    readonly property string current: isFacilitator ? settings.facilitator : settings.unitPrefix
    property string draft: current
    // The model's refusal of the last Save, shown under the field until the person types again.
    property string refusal: ""

    // The page was left, by Back or by a Save that the model accepted.
    signal closeRequested

    function save(): void {
        const answer = isFacilitator ? settings.setFacilitator(draft) : settings.setUnitPrefix(draft);
        refusal = answer;
        if (answer.length === 0)
            closeRequested();
    }

    title: isFacilitator ? qsTr("Facilitator") : qsTr("Unit prefix")
    leadingText: qsTr("Settings")
    trailingText: qsTr("Save")
    trailingEnabled: draft !== current
    onLeadingTriggered: closeRequested()
    onTrailingTriggered: save()
    Component.onCompleted: field.focusField()

    FormField {
        id: field

        objectName: "settingsTextField"
        label: root.isFacilitator ? qsTr("Your name") : qsTr("Prefix")
        text: root.draft
        hint: root.isFacilitator ? qsTr("Shown at the top of the log. A new name applies from the next log you start; the log that is open keeps the name it started with.") : qsTr("The text before the unit number in the log, for example UNIT.")
        errorText: root.refusal
        onTextEdited: text => {
            root.refusal = "";
            root.draft = text;
        }
        onAccepted: root.save()
    }
}
