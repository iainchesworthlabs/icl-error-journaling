// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// Names in shared logs (Stage 14, package I-7; NFR-10): how a person's name is written in text that leaves the device, in
// Copy, Share and a re-shared past log. Three levels: initials (the default), the full name, and "Someone" for no name at all.
// The Review screen and the Interactions board always show the full name, because that text stays on the device. A choice sets
// `exportPreferences.nameExport` and the tick follows the stored level.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var exportPreferences

    readonly property string level: exportPreferences.nameExport

    // The page was left.
    signal closeRequested

    title: qsTr("Names in shared logs")
    leadingText: qsTr("Settings")
    onLeadingTriggered: closeRequested()

    GroupedList {
        width: parent.width
        footer: qsTr("This is how a person's name is written when a log leaves the app, for example Alex Sample as A.S. Review and the Interactions board always show the full name.")

        ListRow {
            objectName: "settingsNamesInitials"
            text: qsTr("Initials")
            checkable: true
            checked: root.level === "initials"
            onActivated: root.exportPreferences.nameExport = "initials"
        }

        ListRow {
            objectName: "settingsNamesFull"
            text: qsTr("Full name")
            checkable: true
            checked: root.level === "full"
            onActivated: root.exportPreferences.nameExport = "full"
        }

        ListRow {
            objectName: "settingsNamesHidden"
            text: qsTr("Someone")
            checkable: true
            checked: root.level === "hidden"
            onActivated: root.exportPreferences.nameExport = "hidden"
        }
    }
}
