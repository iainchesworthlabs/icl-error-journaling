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
//
// Look (Other): a 40 pt pill in a 44 pt touch area, a 10 pt radius, 10 pt padding, an 18 pt search
// Icon in textSecondary 8 pt before the text, the text in 16 pt.
Item {
    id: root

    property string filter: ""
    property string placeholderText: qsTr("Search")
    // What a screen reader says for the field and for the clear button.
    property string accessibleName: placeholderText
    property string clearName: qsTr("Clear search")

    // The padding inside the pill.
    readonly property real padding: Theme.space.tileGap + Theme.border.hairline * 2

    // The user changed the text (typing, deleting, or the clear button).
    signal filterEdited(string filter)
    // Return was pressed.
    signal accepted

    // Puts the cursor in the field, so that the keyboard opens when a sheet is shown.
    function focusField(): void {
        field.forceActiveFocus();
    }

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    // The touch area is 44 pt; the pill drawn in it is 40.
    implicitHeight: Math.max(Theme.size.minTouchTarget, field.implicitHeight)

    onFilterChanged: {
        if (field.text !== filter)
            field.text = filter;
    }
    Component.onCompleted: field.text = filter

    TextField {
        id: field

        objectName: "searchInput"
        anchors.fill: parent
        leftPadding: glyph.x + glyph.width + Theme.space.tileGap
        rightPadding: clearButton.visible ? clearButton.width : root.padding
        topPadding: Theme.border.hairline * 4
        bottomPadding: Theme.border.hairline * 4
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

        background: Item {
            Rectangle {
                objectName: "searchPill"
                anchors.fill: parent
                anchors.topMargin: Theme.border.hairline * 2
                anchors.bottomMargin: Theme.border.hairline * 2
                radius: Theme.radius.small
                color: Theme.colour.searchBackground
                border.width: field.activeFocus ? Theme.border.otherWidth : 0
                border.color: Theme.colour.primaryText
            }
        }

        onTextEdited: root.filterEdited(text)
        onAccepted: root.accepted()
    }

    Icon {
        id: glyph

        objectName: "searchIcon"
        x: root.padding
        y: (parent.height - height) / 2
        size: Theme.size.badge * 3 / 4
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

        Icon {
            anchors.centerIn: parent
            size: Theme.space.screenMargin
            kind: "close"
            colour: Theme.colour.textSecondary
        }
        TapHandler {
            onTapped: clearButton.clear()
        }
    }
}
