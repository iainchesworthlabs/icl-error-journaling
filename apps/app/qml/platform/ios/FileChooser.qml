// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The file chooser of the iPhone build (Stage 4, 4.15). It has the names of `platform/desktop/FileChooser.qml` and does
// nothing: the iOS document picker is Stage 5's (5.7), which replaces this file. The shell asks for a file and answers
// nothing until then, so the import and export rows have no effect on a phone built from this package.
//
// What 5.7 puts here: `chooseToOpen()` shows the picker and ends with `openChosen(path, displayName)` for a file it has
// copied where the app can read it; `chooseToSave(suggestedName)` ends with `saveChosen(path)`.
Item {
    id: root

    // The same filters as the desktop dialog, which the picker turns into document types.
    property var nameFilters: [SetupFile.dialogFilter]
    property string defaultSuffix: SetupFile.exportExtension

    signal openChosen(string path, string displayName)
    signal saveChosen(string path)

    function chooseToOpen(): void {
    }

    function chooseToSave(suggestedName: string): void {
    }
}
