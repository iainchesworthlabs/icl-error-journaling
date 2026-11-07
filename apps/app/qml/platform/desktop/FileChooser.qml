// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtCore
import QtQuick.Dialogs
import ErrorJournalingApp

// The desktop file chooser (Stage 4, 4.15; decision 17): the one place that uses Qt's file dialogs. The shell asks it to
// choose a file to import or a place to export to, and it answers with a signal:
//
//     FileChooser { onOpenChosen: (path, displayName) => importFlow.begin(path, displayName) }
//     chooser.chooseToOpen()
//
// The chooser is the platform's part of the setup file routing. The iPhone build does not have this file: it builds
// `platform/ios/FileChooser.qml`, which has the same names and, until Stage 5's document picker (5.7) replaces it, does
// nothing. `qml_files.cmake` picks the file of the platform, so no screen asks which platform it runs on.
//
// What a dialog gives back is a URL. The chooser turns it into the local path that `settings.loadImport` and
// `settings.exportTo` take, and into the file's name for the import confirmation ("From cs-bingo-board-setup.ejconfig").
Item {
    id: root

    // The name filters of the dialogs, in Qt's form ("Setup files (*.ejconfig *.json)").
    property var nameFilters: [SetupFile.dialogFilter]
    // The extension a saved file gets when the person types a name without one.
    property string defaultSuffix: SetupFile.exportExtension

    // A file to import was chosen.
    signal openChosen(string path, string displayName)
    // A file to export to was chosen.
    signal saveChosen(string path)

    // Shows the dialog to choose a file to import.
    function chooseToOpen(): void {
        openDialog.open();
    }

    // Shows the dialog to choose where to export to, offering `suggestedName`.
    function chooseToSave(suggestedName: string): void {
        saveDialog.currentFile = saveDialog.currentFolder + "/" + suggestedName;
        saveDialog.open();
    }

    // The local path of a file URL: "file:///C:/data/a.json" is "C:/data/a.json" and "file:///home/a.json" is
    // "/home/a.json". Anything that is not a file URL is returned as it is.
    function localPath(file: url): string {
        const text = file.toString();
        if (!text.startsWith("file://"))
            return decodeURIComponent(text);
        const path = decodeURIComponent(text.substring("file://".length));
        return /^\/[A-Za-z]:/.test(path) ? path.substring(1) : path;
    }

    // The name of the file, without its folder.
    function displayName(file: url): string {
        const path = root.localPath(file);
        return path.substring(path.lastIndexOf("/") + 1);
    }

    // What the dialogs do when they are accepted; also how a test chooses a file without a dialog.
    function receiveOpen(file: url): void {
        root.openChosen(root.localPath(file), root.displayName(file));
    }

    function receiveSave(file: url): void {
        root.saveChosen(root.localPath(file));
    }

    FileDialog {
        id: openDialog

        title: qsTr("Choose a setup file")
        fileMode: FileDialog.OpenFile
        nameFilters: root.nameFilters
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: root.receiveOpen(openDialog.selectedFile)
    }

    FileDialog {
        id: saveDialog

        title: qsTr("Export the setup")
        fileMode: FileDialog.SaveFile
        nameFilters: root.nameFilters
        defaultSuffix: root.defaultSuffix
        currentFolder: StandardPaths.writableLocation(StandardPaths.DocumentsLocation)
        onAccepted: root.receiveSave(saveDialog.selectedFile)
    }
}
