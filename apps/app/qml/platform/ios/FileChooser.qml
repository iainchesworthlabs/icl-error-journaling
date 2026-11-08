// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The file chooser of the iPhone build (Stage 4, 4.15; Stage 5, 5.7). It has the names of
// `platform/desktop/FileChooser.qml` and answers from C++ instead of a dialog: `SetupFiles` shows the iOS document picker
// and delivers a file from the picker, or one that AirDrop or Files opened in the application, as `fileReady`, which
// starts the same import flow (confirmation, then `settings.loadImport`).
//
// Export is different on a phone: there is no place to save to, the exporter writes a temporary file and opens the share
// sheet itself, so `chooseToSave` only asks for that and `saveChosen` is never emitted.
Item {
    id: root

    // The picker takes its document types from the application (Info.plist and `document_types.hpp`), so these are kept
    // for the names the shell shares with the desktop chooser.
    property var nameFilters: [SetupFile.dialogFilter]
    property string defaultSuffix: SetupFile.exportExtension

    signal openChosen(string path, string displayName)
    signal saveChosen(string path)
    // A file that was too large or could not be read or opened, or an export that could not be prepared.
    signal failed(string message)

    function chooseToOpen(): void {
        SetupFiles.pick();
    }

    function chooseToSave(suggestedName: string): void {
        SetupFiles.exportSetup();
    }

    Connections {
        function onFileReady(path: string, displayName: string): void {
            root.openChosen(path, displayName);
        }

        function onFailed(message: string): void {
            root.failed(message);
        }

        target: SetupFiles
    }
}
