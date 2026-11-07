// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The note templates of Settings (docs/design/screens/Templates.dc.html, PRF-06): "Add a template" and the templates
// that are chips on the Log screen, each with its question and its output pattern. A row opens the TemplateEdit page,
// which slides in over the list. A template is never removed in v1, because a note holds its template's id.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left.
Item {
    id: root

    required property var settings

    signal closeRequested

    function openTemplate(id: string, name: string, prompt: string, pattern: string): void {
        stack.push(editPage, {
            "templateId": id,
            "templateName": name,
            "prompt": prompt,
            "pattern": pattern
        });
    }

    PageStack {
        id: stack

        anchors.fill: parent
        accessibleName: qsTr("Note templates")
        initialItem: listPage
    }

    Component {
        id: listPage

        EditorPage {
            title: qsTr("Note templates")
            leadingText: qsTr("Back")
            leadingAccessibleName: qsTr("Back to Settings")
            onLeadingTriggered: root.closeRequested()

            Flickable {
                id: flick

                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: column.height + Theme.space.screenMargin
                boundsBehavior: Flickable.StopAtBounds

                Column {
                    id: column

                    x: Theme.space.screenMargin
                    width: flick.width - 2 * Theme.space.screenMargin
                    spacing: Theme.space.sectionGap

                    GroupedList {
                        width: parent.width

                        EditorRow {
                            objectName: "addRow"
                            action: true
                            text: qsTr("Add a template")
                            onActivated: root.openTemplate("", "", "", "")
                        }
                    }

                    GroupedList {
                        objectName: "templateGroup"
                        width: parent.width
                        visible: repeater.count > 0
                        title: qsTr("Chips on the Log screen · %1").arg(repeater.count)
                        footer: qsTr("Tapping a chip asks for the value and adds a note built from the output pattern.")

                        Repeater {
                            id: repeater

                            model: root.settings.templates

                            delegate: EditorRow {
                                required property string templateId
                                required property string name
                                required property string valuePrompt
                                required property string outputPattern

                                objectName: "templateRow"
                                text: name
                                detail: qsTr("Prompt: %1 · Output: %2").arg(valuePrompt).arg(outputPattern)
                                onActivated: root.openTemplate(templateId, name, valuePrompt, outputPattern)
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: editPage

        TemplateEdit {
            settings: root.settings
            onCloseRequested: stack.pop()
        }
    }
}
