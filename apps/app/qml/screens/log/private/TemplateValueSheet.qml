// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The small sheet that asks for the value of a note template (NOTE-04): Cancel and Add in the bar, the template's prompt over
// a focused field, the line the note will have in the log ("In the log"), and the name and pattern of the template under it
// (docs/design/screens/TemplateValue). It is as tall as that content (Sheet.Content) and sits above the keyboard, which
// Sheet looks after.
//
// The sheet checks nothing. Add sends the text to the screen, which asks the session; a refusal comes back as `errorText`
// and is shown under the field, and the sheet stays open with what was typed. `begin()` opens it empty.
//
// `previewText` is the line the note will have, which the screen asks the settings model for (`previewTemplateLine`); the
// sheet only shows it.
Sheet {
    id: root

    required property string templateName
    required property string valuePrompt
    required property string outputPattern
    property string errorText
    property string value
    // The line the note will have in the log, as the renderer makes it for the value typed so far.
    property string previewText

    // Add was chosen, or Return was pressed in the field.
    signal addRequested(string value)

    function begin(): void {
        value = "";
        errorText = "";
        open();
    }

    accessibleName: root.templateName
    detent: Sheet.Content
    contentImplicitHeight: bar.height + fields.height
    onOpened: field.focusField()

    NavBar {
        id: bar

        width: parent.width
        title: root.templateName
        inSheet: true
        leadingText: qsTr("Cancel")
        trailingText: qsTr("Add")
        onLeadingTriggered: root.close()
        onTrailingTriggered: root.addRequested(root.value)
    }

    Column {
        id: fields

        objectName: "templateValueContent"
        x: Theme.space.screenMargin
        y: bar.height
        width: parent.width - 2 * Theme.space.screenMargin

        // The prompt, as the label of the field; it is 15 pt above the field (the field's own label has more room).
        Text {
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            bottomPadding: Theme.border.hairline * 2
            color: Theme.colour.textSecondary
            font: Theme.font.sectionLabel.font
            text: root.valuePrompt.toLocaleUpperCase()
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        FormField {
            id: field

            objectName: "templateValueField"
            label: root.valuePrompt
            labelVisible: false
            compact: true
            text: root.value
            errorText: root.errorText
            onTextEdited: text => {
                root.value = text;
                root.errorText = "";
            }
            onAccepted: root.addRequested(root.value)
        }

        Item {
            width: parent.width
            height: Theme.space.screenMargin + Theme.space.tileGap / 4
        }

        // The line as the log will have it, with the name and pattern of the template under it.
        GroupedList {
            objectName: "templateValuePreview"
            width: parent.width
            title: qsTr("In the log")
            footer: qsTr("Saved with the template “%1”, as “%2”.").arg(root.templateName).arg(root.outputPattern)
            surface: Theme.colour.surfaceRaised

            Item {
                width: parent.width
                height: previewLine.height + 2 * Theme.space.rowPadding
                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("In the log: %1").arg(previewLine.text)

                Text {
                    id: previewLine

                    objectName: "templateValuePreviewText"
                    x: Theme.space.rowPadding
                    y: Theme.space.rowPadding
                    width: parent.width - 2 * Theme.space.rowPadding
                    color: Theme.colour.textPrimary
                    font: Theme.font.previewText.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.previewText.linePitch
                    text: root.previewText
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }
            }
        }
    }
}
