// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The page that adds or edits a note template (docs/design/screens/TemplateEdit.dc.html and TemplateEditInvalid.dc.html,
// PRF-06, NOTE-04): a name, the question the chip asks, and the output pattern with `{value}` where the answer goes.
//
// The pattern is checked while the person types, by `settings.validatePattern`. A pattern the view model refuses (no
// `{value}`, a `{name}` or another placeholder it does not know, over 500 characters) shows its message under the field,
// has no preview ("Fix the pattern to see a preview.") and leaves Save dimmed. A good pattern shows the note it makes,
// with a sample answer in place of `{value}`. Save asks the view model (`addTemplate` or `updateTemplate`); a refusal of
// the name or the question is shown in a banner and cleared by the next edit.
//
// Required: `settings`. A new template has an empty `templateId`; an existing one is opened with its id and its values.
// `closeRequested` is the back button and the end of a good Save.
EditorPage {
    id: root

    required property var settings
    property string templateId: ""
    property string templateName: ""
    property string prompt: ""
    property string pattern: ""

    property string draftName: root.templateName
    property string draftPrompt: root.prompt
    property string draftPattern: root.pattern
    // Why the view model refused a Save.
    property string refusal: ""

    // The message for the pattern now in the field; empty when the renderer accepts it.
    readonly property string patternMessage: root.settings.validatePattern(root.draftPattern)
    readonly property bool patternRefused: root.patternMessage.length > 0
    // The note line the pattern makes, as the Log screen would show it ("10:30 (Free Text) Played Morning Medley"): the
    // model builds it, with its own sample time and answer, or says "Fix the pattern to see a preview.".
    readonly property string previewText: root.settings.previewTemplateLine(root.draftPattern, "", root.sampleSeconds)

    // The time of day the preview shows, 10:30, in seconds since midnight (the mock-up's sample).
    readonly property int sampleSeconds: 37800

    signal closeRequested

    function save(): void {
        if (root.patternRefused || root.refusal.length > 0)
            return;
        const message = root.templateId.length === 0 ? root.settings.addTemplate(root.draftName, root.draftPrompt, root.draftPattern) : root.settings.updateTemplate(root.templateId, root.draftName, root.draftPrompt, root.draftPattern);
        if (message.length === 0)
            root.closeRequested();
        else
            root.refusal = message;
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

        // TemplateEdit: the mock-up's gaps (12 from the title to Name, 20 between the fields, 24 under the hint) are set item
        // by item.
        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
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
                height: root.refusal.length > 0 ? Theme.space.tileGap : Theme.space.tileGap / 2
            }

            FormField {
                objectName: "nameField"
                compact: true
                border: true
                label: qsTr("Name")
                text: root.draftName
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftName = text;
                    root.refusal = "";
                }
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap + Theme.space.tileGap
            }

            FormField {
                objectName: "promptField"
                compact: true
                border: true
                label: qsTr("Prompt")
                text: root.draftPrompt
                onTextEdited: text => {
                    root.draftPrompt = text;
                    root.refusal = "";
                }
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap + Theme.space.tileGap
            }

            FormField {
                objectName: "patternField"
                compact: true
                border: true
                label: qsTr("Output pattern")
                text: root.draftPattern
                hint: qsTr("{value} is replaced by what you type when you tap the chip.")
                errorText: root.draftPattern.length > 0 ? root.patternMessage : ""
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftPattern = text;
                    root.refusal = "";
                }
            }

            // 24 pt under the hint, then the heading and 16 pt to the card (the GroupedList heading is 6, so it is drawn here).
            Item {
                width: parent.width
                height: Theme.space.sectionGap * 2
            }

            Text {
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.sectionLabel.font
                text: qsTr("Preview").toLocaleUpperCase()
                Accessible.role: Accessible.Heading
                Accessible.name: qsTr("Preview")
            }

            Item {
                width: parent.width
                height: Theme.space.screenMargin
            }

            GroupedList {
                width: parent.width
                Accessible.name: qsTr("Preview")

                Item {
                    width: parent.width
                    height: previewLabel.height + 2 * Theme.space.rowPadding

                    Text {
                        id: previewLabel

                        objectName: "previewText"
                        x: Theme.space.rowPadding
                        y: Theme.space.rowPadding
                        width: parent.width - 2 * Theme.space.rowPadding
                        color: root.patternRefused ? Theme.colour.textSecondary : Theme.colour.textPrimary
                        font: Theme.font.previewText.font
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.font.previewText.linePitch
                        text: root.previewText
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
