// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The import refusal (docs/design/screens/ImportErrors.dc.html, PRF-08): the plain-English messages of the core, in the
// order they come in the file, and the reassurance that nothing was changed. The messages are the `importFailed` list of
// the SettingsModel and are shown as they are. "Choose another file" asks for the file dialog again and "Cancel" leaves.
EditorPage {
    id: root

    // The messages, in file order.
    property var messages: []

    signal chooseAnotherRequested
    signal cancelRequested

    title: qsTr("This file can't be imported")
    leadingText: qsTr("Back")
    onLeadingTriggered: root.cancelRequested()

    Flickable {
        id: flick

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: buttons.top
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.sectionGap

            Text {
                objectName: "nothingChanged"
                width: parent.width
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("Nothing was changed. Fix these in the file, in this order, and try again.")
                wrapMode: Text.WordWrap
            }

            GroupedList {
                objectName: "messageGroup"
                width: parent.width
                title: root.messages.length === 1 ? qsTr("1 problem") : qsTr("%1 problems").arg(root.messages.length)

                Repeater {
                    model: root.messages

                    delegate: ListRow {
                        required property string modelData

                        objectName: "messageRow"
                        text: modelData
                        tappable: false
                    }
                }
            }
        }
    }

    Column {
        id: buttons

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space.screenMargin
        anchors.rightMargin: Theme.space.screenMargin
        anchors.bottomMargin: Theme.space.sectionGap
        spacing: Theme.space.tileGap

        PrimaryButton {
            objectName: "chooseAnotherButton"
            width: parent.width
            text: qsTr("Choose another file")
            onClicked: root.chooseAnotherRequested()
        }

        TextButton {
            objectName: "cancelButton"
            width: parent.width
            text: qsTr("Cancel")
            onClicked: root.cancelRequested()
        }
    }
}
