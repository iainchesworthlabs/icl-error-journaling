// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The page of the error prefix (PRF-13): the text in front of the error name on every error line of the log, "T|" unless
// it is changed. A field with Save as the other text pages have, the line as it will be written under it as an example,
// Reset to put "T|" back, and a note that another tool may expect the default. An empty prefix is allowed: the line is
// then the time and the name.
//
// Save asks the model once. If the model refuses (more than 12 characters, a tab or a line break), its words stay under the
// field and the page stays open; if it accepts, the page closes.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings

    readonly property string defaultPrefix: "T|"
    // The value in force, which the draft starts from and is compared with.
    readonly property string current: settings.errorPrefix
    property string draft: current
    // The model's refusal of the last Save, shown under the field until the person types again.
    property string refusal: ""
    // The line as it will be written: an example time and one of the error types.
    readonly property string example: draft.trim().length > 0 ? qsTr("10:05 %1 Wrong Response").arg(draft.trim()) : qsTr("10:05 Wrong Response")

    signal closeRequested

    function save(): void {
        const answer = settings.setErrorPrefix(draft);
        refusal = answer;
        if (answer.length === 0)
            closeRequested();
    }

    function reset(): void {
        refusal = settings.resetErrorPrefix();
        if (refusal.length === 0)
            draft = defaultPrefix;
    }

    title: qsTr("Error prefix")
    leadingText: qsTr("Settings")
    trailingText: qsTr("Save")
    trailingEnabled: draft !== current
    onLeadingTriggered: closeRequested()
    onTrailingTriggered: save()
    onCurrentChanged: draft = current
    Component.onCompleted: field.focusField()

    FormField {
        id: field

        objectName: "errorPrefixField"
        label: qsTr("Prefix")
        text: root.draft
        placeholderText: qsTr("None")
        hint: qsTr("The text before the error name on each error line of the log. Leave it empty for none. Up to 12 characters.")
        errorText: root.refusal
        onTextEdited: text => {
            root.refusal = "";
            root.draft = text;
        }
        onAccepted: root.save()
    }

    // The line as the log will have it.
    Rectangle {
        width: parent.width
        height: exampleLine.height + 2 * Theme.space.rowPadding
        radius: Theme.radius.groupedList
        color: Theme.colour.surfaceRaised
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("Example: %1").arg(root.example)

        Text {
            id: exampleLine

            objectName: "errorPrefixExample"
            x: Theme.space.rowPadding
            y: Theme.space.rowPadding
            width: parent.width - 2 * Theme.space.rowPadding
            color: Theme.colour.textPrimary
            font: Theme.font.monospace.font
            text: root.example
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    PrimaryButton {
        objectName: "errorPrefixReset"
        width: parent.width
        variant: PrimaryButton.Secondary
        text: qsTr("Reset to T|")
        enabled: root.current !== root.defaultPrefix || root.draft !== root.defaultPrefix
        onClicked: root.reset()
    }

    Text {
        objectName: "errorPrefixNote"
        width: parent.width
        leftPadding: Theme.space.tileGap / 2
        rightPadding: Theme.space.tileGap / 2
        color: Theme.colour.textSecondary
        font: Theme.font.footnote.font
        lineHeightMode: Text.FixedHeight
        lineHeight: Theme.font.footnote.linePitch
        text: qsTr("Another tool that reads the shared log may expect the default, T|. Change it only if you are sure.")
        wrapMode: Text.WordWrap
        Accessible.role: Accessible.StaticText
        Accessible.name: text
    }
}
