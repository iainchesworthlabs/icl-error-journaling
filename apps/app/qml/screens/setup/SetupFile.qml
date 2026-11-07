// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// What the shell needs to know about the setup file when it routes `importRequested()` and `exportRequested()`: the name
// a new export is offered under, and which files an import accepts. On the desktop the shell puts these in Qt's file
// dialog; the Stage 5 document picker takes the same list. A setup file is named `cs-bingo-board-setup.ejconfig`, and an
// import accepts `.ejconfig` and `.json` with any name.
QtObject {
    readonly property string defaultFileName: "cs-bingo-board-setup.ejconfig"
    readonly property string exportExtension: "ejconfig"
    readonly property var extensions: ["ejconfig", "json"]
    // The patterns of a file dialog's name filter.
    readonly property var patterns: ["*.ejconfig", "*.json"]
    // The label of the filter in a file dialog.
    readonly property string dialogFilter: qsTr("Setup files (*.ejconfig *.json)")

    // Whether an import accepts a file of this name, whatever the rest of the name is.
    function accepts(fileName: string): bool {
        const lower = fileName.toLowerCase();
        return extensions.some(extension => lower.endsWith("." + extension));
    }
}
