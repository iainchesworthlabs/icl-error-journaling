// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The Import or export page of Settings (docs/design/screens/ImportExport.dc.html, PRF-08): a row to import a setup file,
// a row to export this setup, and a line on what a setup file holds. The rows only emit signals. Choosing the file is the
// platform's part, so the shell routes `importRequested()` and `exportRequested()`: the desktop file dialog now, the
// document picker of Stage 5 on the phone. The shell then calls `settings.loadImport(path)` (the ImportFlow shows what
// comes of it) or `settings.exportTo(path)`, whose result arrives here as `exportFinished` and is shown in a banner that
// the person dismisses.
//
// Required: `settings`, the SettingsModel. `closeRequested` is "Settings" at the top left.
EditorPage {
    id: root

    required property var settings

    // The result of the last export; shown while `exportShown`.
    property bool exportShown: false
    property bool exportOk: false
    property string exportMessage: ""

    signal importRequested
    signal exportRequested
    signal closeRequested

    title: qsTr("Import or export")
    leadingText: qsTr("Back")
    leadingAccessibleName: qsTr("Back to Settings")
    onLeadingTriggered: root.closeRequested()

    Connections {
        function onExportFinished(ok: bool, message: string): void {
            root.exportOk = ok;
            root.exportMessage = message;
            root.exportShown = true;
        }

        target: root.settings
    }

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
                footer: qsTr("A setup file holds error types, locations, note templates and preferences. It does not contain any logs.")

                ListRow {
                    objectName: "importRow"
                    text: qsTr("Import a setup file…")
                    onActivated: root.importRequested()
                }

                ListRow {
                    objectName: "exportRow"
                    text: qsTr("Export this setup…")
                    onActivated: root.exportRequested()
                }
            }

            Banner {
                objectName: "exportBanner"
                width: parent.width
                visible: root.exportShown
                tone: root.exportOk ? Banner.Information : Banner.Error
                title: root.exportOk ? qsTr("Setup exported") : qsTr("Setup not exported")
                message: root.exportMessage
                onCloseRequested: root.exportShown = false
            }
        }
    }
}
