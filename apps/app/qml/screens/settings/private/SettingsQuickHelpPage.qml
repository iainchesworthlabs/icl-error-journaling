// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// Quick help (Stage 10, package 10.6; the mock-up SettingsQuickHelp): the topics of HelpTopics as a list. A row asks for the
// topic's sheet (`topicRequested`) and the shell shows it. The list is the same text as the help buttons of the screens open,
// and it works without a connection.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    // The page was left.
    signal closeRequested
    // A row was activated: the id of the topic.
    signal topicRequested(string topic)

    title: qsTr("Quick help")
    leadingText: qsTr("Settings")
    onLeadingTriggered: closeRequested()

    Text {
        objectName: "quickHelpLine"
        x: Theme.space.tileGap / 2
        width: parent.width - 2 * x
        color: Theme.colour.textSecondary
        font: Theme.font.bodySmall.font
        text: qsTr("Short answers that work without a connection.")
        wrapMode: Text.WordWrap
    }

    GroupedList {
        objectName: "quickHelpList"
        width: parent.width
        footer: qsTr("Each topic ends with a link to the full guide.")

        Repeater {
            model: HelpTopics.topics

            delegate: ListRow {
                id: row

                required property var modelData

                objectName: "quickHelp_" + row.modelData.id
                text: row.modelData.title
                minimumHeight: Theme.size.rowHeightForm
                showChevron: true
                onActivated: root.topicRequested(row.modelData.id)
            }
        }
    }
}
