// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The note templates of Settings (docs/design/screens/Templates.dc.html, PRF-06): "Add a template" and the templates
// that are chips on the Log screen, each with the question it asks and the line it logs ("Asks: Which song? · Logs:
// Played [answer]"). A row opens the TemplateEdit page, which slides in over the list. A template is never removed in
// v1, because a note holds its template's id.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left.
Item {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // form column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

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
            leadingText: qsTr("Settings")
            leadingAccessibleName: qsTr("Back to Settings")
            onLeadingTriggered: root.closeRequested()

            Flickable {
                id: flick

                anchors.fill: parent
                clip: true
                contentWidth: width
                contentHeight: column.height + Theme.space.screenMargin
                boundsBehavior: Flickable.StopAtBounds

                // Templates: 8 pt between the blocks and 8 more above the heading, set item by item.
                Column {
                    id: column

                    x: root.side
                    width: flick.width - 2 * root.side
                    spacing: 0

                    GroupedList {
                        width: parent.width

                        EditorRow {
                            objectName: "addRow"
                            action: true
                            minimumHeight: Theme.size.listRowHeight
                            text: qsTr("Add a template")
                            onActivated: root.openTemplate("", "", "", "")
                        }
                    }

                    Item {
                        width: parent.width
                        height: Theme.space.tileGap * 2
                    }

                    GroupedList {
                        objectName: "templateGroup"
                        width: parent.width
                        visible: repeater.count > 0
                        title: qsTr("Chips on the Log screen · %1").arg(repeater.count)
                        footer: qsTr("Tapping a chip asks its question and adds a note to the log.")

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
                                detail: qsTr("Asks: %1 · Logs: %2").arg(valuePrompt).arg(root.settings.patternToField(outputPattern))
                                chevron: true
                                minimumHeight: Theme.size.rowHeightTall + Theme.space.tileGap / 2
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
