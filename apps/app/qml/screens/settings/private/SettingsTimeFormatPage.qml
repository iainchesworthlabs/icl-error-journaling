// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The time format (Stage 4, package 4.12; PRF-07): a choice between 24-hour time and 12-hour time with seconds. A choice
// asks the model once and the tick moves when the model has saved it. Under the choice, a sample time shows how a log will
// read; the model formats it, in the format in force, so the page never formats a time itself. The format applies to
// every log, past days included.
//
// The model also keeps whether AM and PM are upper or lower case. The page leaves that as it is.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings

    // The moment the sample shows: 14:05:09 UTC on 7 November 2026.
    readonly property double sampleUtcMs: 1794060309000
    readonly property string sampleZone: "UTC"
    // The sample in the format in force. Reading `timeFormatKey` makes the binding run again when the format changes
    // (a key is never empty).
    readonly property string sampleText: settings.timeFormatKey.length > 0 ? settings.formatTime(sampleUtcMs, sampleZone) : ""
    readonly property bool is24Hour: settings.timeStyle === "24h" && !settings.timeSeconds
    readonly property bool is12HourSeconds: settings.timeStyle === "12h" && settings.timeSeconds
    // The model's refusal of the last choice. Empty for none.
    property string message: ""

    // The page was left.
    signal closeRequested

    function choose(style: string, seconds: bool): void {
        message = settings.setTimeFormat(style, seconds, settings.timeMeridiem);
    }

    title: qsTr("Time format")
    leadingText: qsTr("Settings")
    onLeadingTriggered: closeRequested()

    Banner {
        objectName: "settingsTimeMessage"
        width: parent.width
        visible: root.message.length > 0
        title: root.message
        tone: Banner.Error
        onCloseRequested: root.message = ""
    }

    GroupedList {
        width: parent.width
        footer: qsTr("A log shows times like %1. The format applies to every log, including earlier days.").arg(root.sampleText)

        ListRow {
            objectName: "settingsTime24"
            text: qsTr("24-hour")
            checkable: true
            checked: root.is24Hour
            onActivated: root.choose("24h", false)
        }

        ListRow {
            objectName: "settingsTime12"
            text: qsTr("12-hour with seconds")
            checkable: true
            checked: root.is12HourSeconds
            onActivated: root.choose("12h", true)
        }
    }
}
