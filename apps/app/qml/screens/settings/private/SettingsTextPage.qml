// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The page that edits one line of text of the setup (Stage 4, package 4.12; PRF-01, PRF-02): the tester or the unit
// prefix, by `kind`. Save asks the model once. If the model refuses, its words stay under the field, the old value stays
// where it was, and the page stays open; if it accepts, the page closes and the list shows the value when it has been
// saved.
//
// The unit prefix and the totals line (PRF-02, PRF-10) have rows on the Settings list, and only there: one setting has
// one place, so nobody has to wonder which of two copies is the one that counts.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings
    // "tester" or "unitPrefix".
    required property string kind

    readonly property bool isTester: kind === "tester"
    // The value in force, which the draft starts from and is compared with.
    readonly property string current: isTester ? settings.tester : settings.unitPrefix
    property string draft: current
    // The model's refusal of the last Save, shown under the field until the person types again.
    property string refusal: ""
    // The name of the page that this one was opened from, when it is not the usual one (the unit prefix is opened from the
    // Tester page and from the Settings list).
    property string backText: ""

    // The page was left, by Back or by a Save that the model accepted.
    signal closeRequested

    function save(): void {
        const answer = isTester ? settings.setTester(draft) : settings.setUnitPrefix(draft);
        refusal = answer;
        if (answer.length === 0)
            closeRequested();
    }

    title: isTester ? qsTr("Tester") : qsTr("Unit prefix")
    leadingText: backText.length > 0 ? backText : qsTr("Settings")
    trailingText: qsTr("Save")
    trailingEnabled: draft !== current
    onLeadingTriggered: closeRequested()
    onTrailingTriggered: save()
    Component.onCompleted: field.focusField()

    FormField {
        id: field

        objectName: "settingsTextField"
        label: root.isTester ? qsTr("Your name") : qsTr("Prefix")
        text: root.draft
        hint: root.isTester ? qsTr("Shown at the top of the log. A new name applies from the next log you start; the log that is open keeps the name it started with.") : qsTr("The text before the unit number in the log, for example UNIT.")
        errorText: root.refusal
        onTextEdited: text => {
            root.refusal = "";
            root.draft = text;
        }
        onAccepted: root.save()
    }
}
