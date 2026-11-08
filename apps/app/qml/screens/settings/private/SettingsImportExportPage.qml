// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// Import or export of the setup (Stage 4, package 4.12; PRF-08): the two rows, what a setup file holds, and the result of
// the last export. The platform decides how a file is chosen (a dialog on the desktop, Files on the phone), so the rows only
// emit `importRequested` and `exportRequested`; the shell answers with `settings.loadImport(path)` and
// `settings.exportTo(path)`. The model reports the end of an export with `exportFinished`, and this page shows it.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings

    // The result of the last export, in the model's words (`exportResult`: a title such as "Exported to Files" and a
    // message that names the file); hidden until there is one.
    property bool exportShown: false
    property bool exportOk: false
    property string exportTitle: ""
    property string exportText: ""

    // The page was left.
    signal closeRequested
    signal importRequested
    signal exportRequested

    title: qsTr("Import or export")
    leadingText: qsTr("Settings")
    bodySpacing: Theme.space.tileGap
    onLeadingTriggered: closeRequested()

    Connections {
        target: root.settings

        function onExportResult(ok: bool, title: string, fileName: string, message: string): void {
            root.exportOk = ok;
            root.exportTitle = title;
            root.exportText = message;
            root.exportShown = true;
        }
    }

    GroupedList {
        width: parent.width
        footer: qsTr("A setup file holds error types, locations, note templates and preferences. It does not contain any logs. Import and export use Files or AirDrop.")

        ListRow {
            objectName: "settingsImport"
            text: qsTr("Import a setup file…")
            leadingIcon: "file"
            labelColour: Theme.colour.primaryText
            showChevron: true
            onActivated: root.importRequested()
        }

        ListRow {
            objectName: "settingsExport"
            text: qsTr("Export this setup…")
            leadingIcon: "share"
            labelColour: Theme.colour.primaryText
            showChevron: true
            onActivated: {
                root.exportShown = false;
                root.exportRequested();
            }
        }
    }

    Banner {
        objectName: "settingsExportResult"
        width: parent.width
        visible: root.exportShown
        title: root.exportTitle
        message: root.exportText
        tone: root.exportOk ? Banner.Information : Banner.Error
        onCloseRequested: root.exportShown = false
    }
}
