// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// A file chooser that shows no dialog and keeps what it was asked, for the test of the shell (app_flow_test.cpp): a real
// dialog would block a test run on a desktop. It has the names of platform/<kind>/FileChooser.qml; the test chooses a file by
// emitting `openChosen` or `saveChosen`.
Item {
    id: root

    property int opens: 0
    property int saves: 0
    property string suggestedName

    signal openChosen(string path, string displayName)
    signal saveChosen(string path)

    function chooseToOpen(): void {
        root.opens += 1;
    }

    function chooseToSave(name: string): void {
        root.saves += 1;
        root.suggestedName = name;
    }
}
