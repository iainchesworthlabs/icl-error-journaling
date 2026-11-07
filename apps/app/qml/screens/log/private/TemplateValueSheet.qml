// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The small sheet that asks for the value of a note template (NOTE-04): Cancel and Add in the bar, the template's prompt over
// a focused field, and the name and pattern of the template under it (docs/design/screens/TemplateValue). It sits above the
// keyboard, which Sheet looks after.
//
// The sheet checks nothing. Add sends the text to the screen, which asks the session; a refusal comes back as `errorText`
// and is shown under the field, and the sheet stays open with what was typed. `begin()` opens it empty.
Sheet {
    id: root

    required property string templateName
    required property string valuePrompt
    required property string outputPattern
    property string errorText
    property string value

    // Add was chosen, or Return was pressed in the field.
    signal addRequested(string value)

    function begin(): void {
        value = "";
        errorText = "";
        open();
    }

    accessibleName: root.templateName
    detent: Sheet.Medium
    onOpened: field.focusField()

    NavBar {
        id: bar

        width: parent.width
        title: root.templateName
        leadingText: qsTr("Cancel")
        trailingText: qsTr("Add")
        onLeadingTriggered: root.close()
        onTrailingTriggered: root.addRequested(root.value)
    }

    FormField {
        id: field

        objectName: "templateValueField"
        x: Theme.space.screenMargin
        y: bar.height + Theme.space.sectionGap
        width: parent.width - 2 * Theme.space.screenMargin
        label: root.valuePrompt
        text: root.value
        errorText: root.errorText
        onTextEdited: text => {
            root.value = text;
            root.errorText = "";
        }
        onAccepted: root.addRequested(root.value)
    }

    Text {
        objectName: "templateValueNote"
        x: Theme.space.screenMargin + Theme.space.tileGap / 2
        y: field.y + field.height + Theme.space.sectionGap
        width: parent.width - 2 * x
        color: Theme.colour.textSecondary
        font: Theme.font.caption.font
        text: qsTr("Saved with the template “%1”, as “%2”.").arg(root.templateName).arg(root.outputPattern)
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
