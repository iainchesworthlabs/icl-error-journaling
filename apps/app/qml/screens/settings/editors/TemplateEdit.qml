// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The page that adds or edits a note template (docs/design/screens/TemplateEdit.dc.html and TemplateEditInvalid.dc.html,
// PRF-06, NOTE-04). One sentence says what a template is for; then three plain fields, an example, and nothing to explain:
//
//   Name              the chip on the Log screen says this
//   Question to ask   what the person is asked when they tap the chip (`valuePrompt` in the file)
//   Line in the log   what is written; "Insert answer" puts the answer slot, shown as "[answer]", at the cursor
//                     (`outputPattern` in the file, which still stores {value}: SettingsModel.patternToField and
//                     patternFromField change one into the other, so nobody types curly brackets)
//
// A new template starts with the question made from its name ("Played song?") and the line "Played song: [answer]"; each
// follows the name until the person edits it. The line is checked while the person types, by `settings.validatePattern`.
// A line the view model refuses (no answer in it, a curly bracket on its own, another word in brackets, over 500
// characters) shows its message under the field, has no example ("Fix the line to see an example.") and leaves Save
// dimmed. A good line shows "In the log: 10:30 Played Morning Medley", made by the model with a sample time
// and answer. Save asks the view model (`addTemplate` or `updateTemplate`); a refusal of the name or the question is
// shown in a banner and cleared by the next edit.
//
// Required: `settings`. A new template has an empty `templateId`; an existing one is opened with its id and its values
// (`pattern` is the stored pattern, with {value}). `closeRequested` is the back button and the end of a good Save.
EditorPage {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // form column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

    required property var settings
    property string templateId: ""
    property string templateName: ""
    property string prompt: ""
    property string pattern: ""

    // A new template has no id yet; its question and line follow the name until the person edits them.
    readonly property bool isNew: root.templateId.length === 0
    property bool promptEdited: false
    property bool lineEdited: false

    property string draftName: root.templateName
    property string draftPrompt: root.prompt
    // What the "Line in the log" field shows ("Played [answer]"), and the stored pattern it stands for ("Played {value}").
    property string draftLine: root.settings.patternToField(root.pattern)
    readonly property string draftPattern: root.settings.patternFromField(root.draftLine)
    // The answer slot as the field writes it: "[answer]".
    readonly property string answerToken: root.settings.patternToField("{value}")
    // Why the view model refused a Save.
    property string refusal: ""

    // The message for the line now in the field; empty when the renderer accepts it.
    readonly property string patternMessage: root.settings.validatePattern(root.draftPattern)
    readonly property bool patternRefused: root.patternMessage.length > 0
    // The note line the pattern makes, as the Log screen would show it ("10:30 Played Morning Medley"): the
    // model builds it, with its own sample time and answer, or says "Fix the line to see an example.".
    readonly property string previewLine: root.settings.previewTemplateLine(root.draftPattern, "", root.sampleSeconds)
    readonly property string previewText: root.patternRefused ? root.previewLine : qsTr("In the log: %1").arg(root.previewLine)

    // The time of day the example shows, 10:30, in seconds since midnight (the mock-up's sample).
    readonly property int sampleSeconds: 37800

    signal closeRequested

    function save(): void {
        if (root.patternRefused || root.refusal.length > 0)
            return;
        const message = root.isNew ? root.settings.addTemplate(root.draftName, root.draftPrompt, root.draftPattern) : root.settings.updateTemplate(root.templateId, root.draftName, root.draftPrompt, root.draftPattern);
        if (message.length === 0)
            root.closeRequested();
        else
            root.refusal = message;
    }

    // The name was edited: a new template's question and line follow it until the person has edited them.
    function nameEdited(text: string): void {
        root.draftName = text;
        root.refusal = "";
        if (!root.isNew)
            return;
        if (!root.promptEdited)
            root.draftPrompt = root.settings.defaultTemplateQuestion(text);
        if (!root.lineEdited)
            root.draftLine = root.settings.defaultTemplateLine(text);
    }

    title: root.templateName.length > 0 ? root.templateName : qsTr("New template")
    leadingText: qsTr("Note templates")
    leadingAccessibleName: qsTr("Back to Note templates")
    trailingText: qsTr("Save")
    trailingEnabled: !root.patternRefused && root.refusal.length === 0
    onLeadingTriggered: root.closeRequested()
    onTrailingTriggered: root.save()

    Flickable {
        id: flick

        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin
        boundsBehavior: Flickable.StopAtBounds

        // TemplateEdit: the mock-up's gaps (20 between a hint and the next label, 8 above the button, 24 above the
        // example) are set item by item.
        Column {
            id: column

            x: root.side
            width: flick.width - 2 * root.side
            spacing: 0

            Banner {
                objectName: "refusalBanner"
                width: parent.width
                visible: root.refusal.length > 0
                tone: Banner.Error
                title: qsTr("This template was not saved")
                message: root.refusal
                closable: false
            }

            Item {
                width: parent.width
                height: root.refusal.length > 0 ? Theme.space.tileGap : 0
            }

            // What a template is for, in one sentence.
            Text {
                objectName: "introText"
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.description.font
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.font.description.linePitch
                text: qsTr("Templates are quick notes you log often. Tap one on the Log screen and answer one question.")
                wrapMode: Text.WordWrap
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap + Theme.space.tileGap
            }

            FormField {
                objectName: "nameField"
                compact: true
                border: true
                label: qsTr("Name")
                text: root.draftName
                hint: qsTr("This is what the chip on the Log screen says.")
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => root.nameEdited(text)
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap + Theme.space.tileGap
            }

            FormField {
                objectName: "promptField"
                compact: true
                border: true
                label: qsTr("Question to ask")
                text: root.draftPrompt
                hint: qsTr("This is what you are asked when you tap the chip.")
                onTextEdited: text => {
                    root.promptEdited = true;
                    root.draftPrompt = text;
                    root.refusal = "";
                }
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap + Theme.space.tileGap
            }

            FormField {
                id: lineField

                objectName: "patternField"
                compact: true
                border: true
                label: qsTr("Line in the log")
                text: root.draftLine
                hint: qsTr("This is what gets written. Tap Insert answer to put %1 where the answer goes.").arg(root.answerToken)
                errorText: root.draftLine.length > 0 ? root.patternMessage : ""
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.lineEdited = true;
                    root.draftLine = text;
                    root.refusal = "";
                }
            }

            Item {
                width: parent.width
                height: Theme.space.tileGap
            }

            // Puts the answer slot at the cursor, in place of any selected text, and keeps the keyboard up.
            PrimaryButton {
                objectName: "insertAnswerButton"
                width: Math.min(implicitWidth, parent.width)
                variant: PrimaryButton.Secondary
                compact: true
                iconKind: "plus"
                text: qsTr("Insert answer")
                accessibleName: qsTr("Insert answer")
                Accessible.description: qsTr("Puts %1 in the line at the cursor. It is replaced by the answer you type when you tap the chip.").arg(root.answerToken)
                onClicked: lineField.insertAtCursor(root.answerToken)
            }

            // 24 pt under the button, then the heading and 8 pt to the card (the GroupedList heading is 6, so it is drawn here).
            Item {
                width: parent.width
                height: Theme.space.sectionGap * 2
            }

            Text {
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.sectionLabel.font
                text: qsTr("Example").toLocaleUpperCase()
                Accessible.role: Accessible.Heading
                Accessible.name: qsTr("Example")
            }

            Item {
                width: parent.width
                height: Theme.space.tileGap
            }

            // The example is a tinted card with the line at body size, so it is what the eye finds first.
            Rectangle {
                objectName: "exampleCard"
                width: parent.width
                height: previewLabel.height + 2 * Theme.space.rowPadding
                radius: Theme.radius.groupedList
                color: Theme.colour.primaryTint
                border.width: Theme.border.hairline
                border.color: Theme.colour.border
                Accessible.role: Accessible.StaticText
                Accessible.name: previewLabel.text

                Text {
                    id: previewLabel

                    objectName: "previewText"
                    x: Theme.space.rowPadding
                    y: Theme.space.rowPadding
                    width: parent.width - 2 * Theme.space.rowPadding
                    color: root.patternRefused ? Theme.colour.textSecondary : Theme.colour.textPrimary
                    font: Theme.font.body.font
                    text: root.previewText
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }
            }
        }
    }
}
