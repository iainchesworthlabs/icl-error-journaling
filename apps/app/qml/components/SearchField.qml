// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The search field of the Other picker: a grey pill with a magnifier, the typed text and a clear button. It is a Basic
// TextField with its look replaced; the control supplies the text input, the keyboard and the editing accessibility.
//
// `filter` is the text. A screen sets it (`filter: picker.filter`) and listens to `filterEdited(filter)`, which fires once
// for every change the user makes, and once when the clear button empties the field. The field never assigns `filter`
// itself, so a binding on it is never broken.
Item {
    id: root

    property string filter: ""
    property string placeholderText: qsTr("Search")
    // What a screen reader says for the field and for the clear button.
    property string accessibleName: placeholderText
    property string clearName: qsTr("Clear search")

    // The user changed the text (typing, deleting, or the clear button).
    signal filterEdited(string filter)
    // Return was pressed.
    signal accepted

    // Puts the cursor in the field, so that the keyboard opens when a sheet is shown.
    function focusField(): void {
        field.forceActiveFocus();
    }

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: Math.max(Theme.size.fieldHeight, field.implicitHeight)

    onFilterChanged: {
        if (field.text !== filter)
            field.text = filter;
    }
    Component.onCompleted: field.text = filter

    TextField {
        id: field

        objectName: "searchInput"
        anchors.fill: parent
        leftPadding: Theme.space.rowPadding + glyph.width + Theme.space.tileGap
        rightPadding: clearButton.visible ? clearButton.width : Theme.space.rowPadding
        topPadding: Theme.space.tileGap
        bottomPadding: Theme.space.tileGap
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.colour.textPrimary
        font: Theme.font.body.font
        placeholderText: root.placeholderText
        placeholderTextColor: Theme.colour.textPlaceholder
        selectionColor: Theme.colour.primaryTint
        selectedTextColor: Theme.colour.textPrimary
        inputMethodHints: Qt.ImhNoPredictiveText
        EnterKey.type: Qt.EnterKeySearch

        Accessible.role: Accessible.EditableText
        Accessible.name: root.accessibleName
        Accessible.searchEdit: true

        background: Rectangle {
            radius: Theme.radius.field
            color: Theme.colour.searchBackground
            border.width: field.activeFocus ? Theme.border.otherWidth : 0
            border.color: Theme.colour.primaryText
        }

        onTextEdited: root.filterEdited(text)
        onAccepted: root.accepted()
    }

    InputGlyph {
        id: glyph

        x: Theme.space.rowPadding
        y: (parent.height - height) / 2
        kind: "search"
        colour: Theme.colour.textSecondary
    }

    // The clear button: a cross in a touch target of the full height of the field.
    Item {
        id: clearButton

        objectName: "clearButton"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Theme.size.minTouchTarget
        visible: field.text.length > 0

        Accessible.role: Accessible.Button
        Accessible.name: root.clearName
        Accessible.focusable: true
        Accessible.onPressAction: clearButton.clear()

        function clear(): void {
            field.text = "";
            root.filterEdited("");
            field.forceActiveFocus();
        }

        InputGlyph {
            anchors.centerIn: parent
            kind: "clear"
            colour: Theme.colour.textSecondary
        }
        TapHandler {
            onTapped: clearButton.clear()
        }
    }
}
