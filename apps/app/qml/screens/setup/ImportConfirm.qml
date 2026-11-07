// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The import confirmation (docs/design/screens/ImportConfirm.dc.html, PRF-08): what the file holds, from the counts that
// `importPrepared` gave, and what importing does. Nothing has changed yet; "Replace my setup" is the person's go-ahead
// and "Cancel" leaves everything as it was. The ImportFlow wires the two to `applyImport` and `discardImport`.
//
// The screen takes plain values, so a test or the flow supplies them. `replacing` is false on first launch, when there is
// no setup to replace, and the wording follows.
EditorPage {
    id: root

    property string fileName: ""
    property int errorTypeCount: 0
    property int locationCount: 0
    property int templateCount: 0
    property bool replacing: true
    // Why the last attempt to apply the file failed; empty for none.
    property string problem: ""

    signal replaceRequested
    signal cancelRequested

    title: qsTr("Import this setup?")
    leadingText: qsTr("Cancel")
    leadingBack: false
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
                objectName: "fromText"
                width: parent.width
                visible: root.fileName.length > 0
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("From %1").arg(root.fileName)
                wrapMode: Text.WordWrap
            }

            GroupedList {
                width: parent.width

                ListRow {
                    objectName: "errorTypesRow"
                    text: qsTr("Error types")
                    value: String(root.errorTypeCount)
                    tappable: false
                }

                ListRow {
                    objectName: "locationsRow"
                    text: qsTr("Locations")
                    value: String(root.locationCount)
                    tappable: false
                }

                ListRow {
                    objectName: "templatesRow"
                    text: qsTr("Note templates")
                    value: String(root.templateCount)
                    tappable: false
                }

                ListRow {
                    objectName: "preferencesRow"
                    text: qsTr("Preferences")
                    value: qsTr("Included")
                    tappable: false
                }
            }

            Banner {
                objectName: "replaceBanner"
                width: parent.width
                tone: root.replacing ? Banner.Warning : Banner.Information
                closable: false
                title: root.replacing ? qsTr("This replaces your current setup") : qsTr("This becomes your setup")
                message: root.replacing ? qsTr("Your board, locations, templates and preferences are swapped for the ones in the file. Past logs aren't touched.") : qsTr("Your board, locations, templates and preferences are taken from the file.")
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

        Banner {
            objectName: "problemBanner"
            width: parent.width
            visible: root.problem.length > 0
            tone: Banner.Error
            closable: false
            title: qsTr("The setup was not replaced")
            message: root.problem
        }

        PrimaryButton {
            objectName: "replaceButton"
            width: parent.width
            text: root.replacing ? qsTr("Replace my setup") : qsTr("Use this setup")
            onClicked: root.replaceRequested()
        }

        TextButton {
            objectName: "cancelButton"
            width: parent.width
            text: qsTr("Cancel")
            onClicked: root.cancelRequested()
        }
    }
}
