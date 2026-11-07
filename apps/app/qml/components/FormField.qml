// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// A labelled text field with its message under it: the label in small capitals, the field in a white 14 pt card, and
// either a hint ("Type the number only.") or, when the model refuses the value, the refusal (`errorText`) in the
// destructive colour. The message is also the accessible description of the field, so a screen reader says it with the
// field, and a refused field has a 2 pt border as well as red text. A valid value can show a check mark (`valid`).
//
// It is a Basic TextField with its look replaced. `text` is the value: a screen sets it and listens to
// `textEdited(text)`, which fires for every change the user makes; the field never assigns `text` itself, so a binding
// on it is never broken. The screen decides what is valid (it asks the view model) and sets `errorText`; the Save state
// follows from `invalid`.
Item {
    id: root

    required property string label
    property string text: ""
    property string placeholderText: ""
    // A fixed start the user does not type ("UNIT-").
    property string prefixText: ""
    property string hint: ""
    property string errorText: ""
    property bool valid: false
    property bool labelVisible: true
    property int inputMethodHints: Qt.ImhNone
    property string accessibleName: label

    readonly property bool invalid: errorText.length > 0
    readonly property bool hasFocus: field.activeFocus

    signal textEdited(string text)
    signal accepted

    function focusField(): void {
        field.forceActiveFocus();
    }

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: heading.height + box.height + message.height
    width: parent ? parent.width : implicitWidth

    onTextChanged: {
        if (field.text !== text)
            field.text = text;
    }
    Component.onCompleted: field.text = text

    Text {
        id: heading

        x: Theme.space.tileGap / 2
        width: parent.width - 2 * x
        height: visible ? implicitHeight + Theme.space.tileGap : 0
        visible: root.labelVisible
        color: Theme.colour.textSecondary
        font: Theme.font.sectionLabel.font
        text: root.label.toLocaleUpperCase()
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    Item {
        id: box

        y: heading.height
        width: parent.width
        height: Math.max(Theme.size.fieldHeight + Theme.space.tileGap, field.implicitHeight)

        TextField {
            id: field

            objectName: "formInput"
            anchors.fill: parent
            leftPadding: Theme.space.rowPadding + (prefix.visible ? prefix.width : 0)
            rightPadding: Theme.space.rowPadding + (root.valid && !root.invalid ? checkMark.width + Theme.space.tileGap : 0)
            topPadding: Theme.space.tileGap
            bottomPadding: Theme.space.tileGap
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.colour.textPrimary
            font: Theme.font.sheetTitle.font
            placeholderText: root.placeholderText
            placeholderTextColor: Theme.colour.textPlaceholder
            selectionColor: Theme.colour.primaryTint
            selectedTextColor: Theme.colour.textPrimary
            inputMethodHints: root.inputMethodHints

            Accessible.role: Accessible.EditableText
            Accessible.name: root.accessibleName
            Accessible.description: root.invalid ? root.errorText : root.hint

            background: Rectangle {
                radius: Theme.radius.groupedList
                color: Theme.colour.surface
                border.width: root.invalid || field.activeFocus ? Theme.border.hairline * 2 : 0
                border.color: root.invalid ? Theme.colour.destructive : Theme.colour.primaryText
            }

            onTextEdited: root.textEdited(text)
            onAccepted: root.accepted()
        }

        Text {
            id: prefix

            x: Theme.space.rowPadding
            y: (box.height - height) / 2
            visible: root.prefixText.length > 0
            color: Theme.colour.textSecondary
            font: Theme.font.sheetTitle.font
            text: root.prefixText
            Accessible.ignored: true
        }

        InputGlyph {
            id: checkMark

            x: parent.width - Theme.space.rowPadding - width
            y: (parent.height - height) / 2
            width: Theme.space.screenMargin + Theme.space.tileGap / 2
            visible: root.valid && !root.invalid
            kind: "check"
            colour: Theme.colour.primaryText
        }
    }

    Text {
        id: message

        x: Theme.space.tileGap / 2
        y: box.y + box.height
        width: parent.width - 2 * x
        height: visible ? implicitHeight + Theme.space.tileGap : 0
        visible: text.length > 0
        verticalAlignment: Text.AlignBottom
        color: root.invalid ? Theme.colour.destructive : Theme.colour.textSecondary
        font: Theme.font.caption.font
        text: root.invalid ? root.errorText : root.hint
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }
}
