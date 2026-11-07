// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The new-day prompt (Stage 4, package 4.11; DAY-01): an Alert over the whole window that names the new date, says how many
// entries the open log has and whether it has been shared, and offers three things:
//
//   Start a new log                the default action: `day.acceptNewLog()`
//   Share <day>'s log first        `day.shareFirst()`; only while the log is `unshared`
//   Keep <day>'s log open          `day.keepOpen()`
//
// It is declared in the SheetHost that the shell keeps over everything (a later sheet is over an earlier one), so it covers the
// tab bar too, and, being an Alert, it is not dismissed by a tap outside it, by a drag or by Escape.
//
//   day    the DayWatcher (promptVisible, newDateText, openLogDayText, openLogEntryCount, unshared and the three invokables)
//
// The alert follows `day.promptVisible` and nothing else decides whether it is there. A button closes an Alert, so when the model
// still has the prompt up after a button (a new log that could not be started, or a share that has been started: the prompt
// stays up whatever the outcome of the share) the alert comes back as soon as it has closed. `unshared` turns off when the share
// has been recorded, even while the alert is up, and the warning and the "Share first" button go with it.
//
// The words come from the model, unedited, and keep their last values while the prompt is hidden, so the card does not go blank as
// it fades. The screen fires no haptic.
Alert {
    id: root

    required property var day

    // The last "Start a new log" was refused: the prompt stays up and says so.
    property bool startFailed: false

    readonly property string entriesText: root.day.openLogEntryCount === 1 ? qsTr("1 entry") : qsTr("%1 entries").arg(root.day.openLogEntryCount)

    title: qsTr("It’s %1").arg(root.day.newDateText)
    message: root.startFailed ? qsTr("A new log could not be started. Nothing was changed, and %1’s log is still open.").arg(root.day.openLogDayText) : root.day.unshared ? qsTr("%1’s log has %2 and hasn’t been shared yet. Starting a new log keeps it in History.").arg(root.day.openLogDayText).arg(root.entriesText) : qsTr("%1’s log has %2. Starting a new log keeps it in History.").arg(root.day.openLogDayText).arg(root.entriesText)
    icon: Alert.Calendar
    confirmText: qsTr("Start a new log")
    alternateText: root.day.unshared ? qsTr("Share %1’s log first").arg(root.day.openLogDayText) : ""
    cancelText: qsTr("Keep %1’s log open").arg(root.day.openLogDayText)

    // Makes the alert match the model: up while the prompt is up, down when it is not.
    function sync(): void {
        if (root.day.promptVisible) {
            if (!root.isOpen)
                root.open();
        } else {
            root.startFailed = false;
            if (root.isOpen)
                root.close();
        }
    }

    onConfirmed: {
        // A refusal leaves the prompt up; the alert has closed itself, so it is opened again at once. When the new log is
        // on its way the alert stays closed: the model takes the prompt down after the commit, and if the commit fails the
        // alert comes back when it has closed.
        root.startFailed = !root.day.acceptNewLog();
        if (root.startFailed)
            root.open();
    }
    onAlternated: {
        root.startFailed = false;
        root.day.shareFirst();
    }
    onCancelled: root.day.keepOpen()
    onClosed: root.sync()
    Component.onCompleted: root.sync()

    Connections {
        target: root.day

        function onPromptChanged(): void {
            root.sync();
        }
    }
}
