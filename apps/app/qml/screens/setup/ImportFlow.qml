// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// What happens after a setup file has been chosen (PRF-08, PRF-09): the confirmation when the file is good, the list of
// problems when it is not. It fills its parent and shows nothing until a file has been read, so the shell puts one over
// the whole window, above the first-launch screen and above Settings alike.
//
// The shell routes `importRequested()` (from FirstLaunch or ImportExport) to the platform's file dialog, then calls
// `begin(path, name)` with the chosen file. The flow calls `settings.loadImport(path)` and waits for the model:
//
//   importPrepared(...)   shows ImportConfirm with the counts. "Replace my setup" calls `applyImport`; the flow closes when
//                         the save has finished, or shows the reason on the page when it failed. "Cancel" calls
//                         `discardImport` and changes nothing.
//   importFailed(...)     shows ImportErrors with the messages in file order. "Choose another file" emits
//                         `importRequested()` again, "Cancel" closes. Nothing has changed.
//
// `finished()` is emitted when the flow closes, whichever way.
Item {
    id: root

    required property var settings

    // The name of the file, for "From ...". The shell gives it to `begin`.
    property string fileName: ""
    // "none", "confirm" or "errors".
    property string page: "none"
    property int errorTypeCount: 0
    property int locationCount: 0
    property int templateCount: 0
    property var messages: []
    property string problem: ""
    // The file is being applied: the flow closes when the setup has changed.
    property bool applying: false

    readonly property bool active: root.page !== "none"

    // The person asked for another file.
    signal importRequested
    signal finished

    function begin(path: string, name: string): void {
        root.fileName = name;
        root.settings.loadImport(path);
    }

    function close(): void {
        root.page = "none";
        root.applying = false;
        root.problem = "";
        root.finished();
    }

    visible: root.active

    Connections {
        function onImportPrepared(errorTypes: int, locations: int, templates: int): void {
            root.errorTypeCount = errorTypes;
            root.locationCount = locations;
            root.templateCount = templates;
            root.problem = "";
            root.applying = false;
            root.page = "confirm";
        }

        function onImportFailed(messages: var): void {
            root.messages = messages;
            root.applying = false;
            root.page = "errors";
        }

        function onWriteFailed(operation: string, message: string): void {
            if (root.applying) {
                root.applying = false;
                root.problem = message;
            }
        }

        function onConfigurationChanged(): void {
            if (root.applying)
                root.close();
        }

        target: root.settings
    }

    ImportConfirm {
        objectName: "importConfirm"
        anchors.fill: parent
        visible: root.page === "confirm"
        fileName: root.fileName
        errorTypeCount: root.errorTypeCount
        locationCount: root.locationCount
        templateCount: root.templateCount
        replacing: root.settings.configured
        problem: root.problem
        onReplaceRequested: {
            root.applying = true;
            root.problem = "";
            root.settings.applyImport();
        }
        onCancelRequested: {
            root.settings.discardImport();
            root.close();
        }
    }

    ImportErrors {
        objectName: "importErrors"
        anchors.fill: parent
        visible: root.page === "errors"
        messages: root.messages
        onChooseAnotherRequested: {
            root.close();
            root.importRequested();
        }
        onCancelRequested: root.close()
    }
}
