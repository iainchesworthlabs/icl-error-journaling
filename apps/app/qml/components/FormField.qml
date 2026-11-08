// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// A labelled text field with its message under it: the label in small capitals, the field in a white card, and either a
// hint ("Type the number only.") or, when the model refuses the value, the refusal (`errorText`). The message is also the
// accessible description of the field, so a screen reader says it with the field. A refused field has a 2 pt
// destructive border and its message starts with a warning sign, so the refusal is not colour alone. A valid value can
// show a check mark (`valid`).
//
// Options:
//   valueFont  the font of the value and the prefix: Theme.font.fieldText.font (17/400) by default,
//              fieldValueLarge (20/600) for the Header unit and version, fieldValue (18/600) for the version sheet
//   compact    the 48 pt field with a 12 pt radius (FirstLaunchManual, the settings editors); the default field is
//              52 pt with a 14 pt radius (Header)
//   border     a 1 pt `border` colour outline at rest; focus is always a 2 pt primaryText outline
//   surface    the fill: surface by default, surfaceRaised on a sheet (so the field shows on the dark sheet)
//   prefixText a fixed start the user does not type ("UNIT-"), in textSecondary, 2 pt before the value
// The message is 13 pt with a 1.35 line height: the hint in textSecondary; the refusal in textPrimary after a 16 pt
// warning sign in destructive, 6 pt from the text.
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
    property font valueFont: Theme.font.fieldText.font
    property bool compact: false
    property bool border: false
    property color surface: Theme.colour.surface

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

        objectName: "formBox"
        y: heading.height
        width: parent.width
        height: Math.max(root.compact ? Theme.size.compactFieldHeight : Theme.size.rowHeightForm, field.implicitHeight)

        TextField {
            id: field

            objectName: "formInput"
            anchors.fill: parent
            leftPadding: Theme.space.rowPadding + (prefix.visible ? prefix.width + Theme.border.hairline * 2 : 0)
            rightPadding: Theme.space.rowPadding + (root.valid && !root.invalid ? checkMark.width + Theme.space.tileGap : 0)
            topPadding: Theme.space.tileGap
            bottomPadding: Theme.space.tileGap
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.colour.textPrimary
            font: root.valueFont
            placeholderText: root.placeholderText
            placeholderTextColor: Theme.colour.textPlaceholder
            selectionColor: Theme.colour.primaryTint
            selectedTextColor: Theme.colour.textPrimary
            inputMethodHints: root.inputMethodHints

            Accessible.role: Accessible.EditableText
            Accessible.name: root.accessibleName
            Accessible.description: root.invalid ? root.errorText : root.hint

            background: Rectangle {
                objectName: "formBackground"
                radius: root.compact ? Theme.radius.field : Theme.radius.groupedList
                color: root.surface
                border.width: root.invalid || field.activeFocus ? Theme.border.hairline * 2 : root.border ? Theme.border.hairline : 0
                border.color: root.invalid ? Theme.colour.destructive : field.activeFocus ? Theme.colour.primaryText : Theme.colour.border
            }

            onTextEdited: root.textEdited(text)
            onAccepted: root.accepted()
        }

        Text {
            id: prefix

            objectName: "formPrefix"
            x: Theme.space.rowPadding
            y: (box.height - height) / 2
            visible: root.prefixText.length > 0
            color: Theme.colour.textSecondary
            font: root.valueFont
            text: root.prefixText
            Accessible.ignored: true
        }

        Icon {
            id: checkMark

            x: parent.width - Theme.space.rowPadding - width
            y: (parent.height - height) / 2
            size: Theme.size.badge * 5 / 6
            visible: root.valid && !root.invalid
            kind: "check"
            colour: Theme.colour.primaryText
        }
    }

    Item {
        id: message

        objectName: "formMessage"
        readonly property bool shown: root.invalid || root.hint.length > 0

        x: Theme.space.tileGap / 2
        y: box.y + box.height
        width: parent.width - 2 * x
        height: shown ? messageText.height + Theme.space.tileGap : 0
        visible: shown

        Icon {
            id: messageIcon

            objectName: "formErrorIcon"
            visible: root.invalid
            y: Theme.space.tileGap + Math.max(0, (Theme.font.fieldMessage.linePitch - height) / 2)
            size: Theme.size.badge * 2 / 3
            kind: "warning"
            colour: Theme.colour.destructive
        }

        Text {
            id: messageText

            objectName: "formMessageText"
            x: root.invalid ? messageIcon.width + Theme.border.otherWidth * 4 : 0
            y: Theme.space.tileGap
            width: parent.width - x
            color: root.invalid ? Theme.colour.textPrimary : Theme.colour.textSecondary
            font: Theme.font.fieldMessage.font
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.font.fieldMessage.linePitch
            text: root.invalid ? root.errorText : root.hint
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }
}
