// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQml.Models
import ErrorJournalingApp

// The Settings list (Stage 4, package 4.12; PRF-01 to 07, 10): the miniature board on top of it and, under the board, the
// rows. The board is MiniBoard over `settings.boardLayout`; a move it asks for goes to `settings.moveBoardTile` and the
// choice of a type for a tile is the screen's (`replaceRequested`), which opens the picker and calls
// `settings.assignBoardTile`. The rows that open an editor of the lists emit a signal and the shell shows the editor
// (package 4.13); the others open a page of this screen.
//
// A refusal of the model, a write that failed and a setup that could not be read are shown inline, above the board.
// While the setup is being read (`settings.loading`) the page shows its skeleton and offers nothing.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings

    signal errorTypesRequested
    signal templatesRequested
    signal locationsRequested
    signal facilitatorRequested
    signal unitPrefixRequested
    signal timeFormatRequested
    signal importExportRequested
    // A tile of the board is to get another type: its position (1 to 11) and the type it has now.
    signal replaceRequested(int position, string typeId)

    // The last refusal or failure, in the model's words. Empty for none.
    property string message: ""
    // A tile that was asked to move, and where it should arrive: the selection goes with it when the model has moved it.
    // MiniBoard follows a tile that the model moves while the request is being made; SettingsModel moves it when the save
    // has finished, so the screen follows it then.
    property string followTypeId: ""
    property int followPosition: 0
    readonly property bool loading: settings.loading

    // Shows what a call of `settings` answered: "" is success and clears an earlier message.
    function show(answer: string): void {
        message = answer;
        if (answer.length > 0)
            followTypeId = "";
    }

    // Asks for a move, and remembers which tile it was if it is the selected one.
    function move(fromPosition: int, toPosition: int): void {
        if (board.selectedPosition === fromPosition) {
            followTypeId = board.tileItem(fromPosition - 1).typeId;
            followPosition = toPosition;
        }
        show(settings.moveBoardTile(fromPosition, toPosition));
    }

    // The board changed: if the tile that was asked to move has arrived, the selection moves to it.
    function followSelected(): void {
        if (followTypeId.length === 0)
            return;
        const arrived = board.tileItem(followPosition - 1);
        if (arrived && arrived.typeId === followTypeId) {
            board.selectedPosition = followPosition;
            followTypeId = "";
        }
    }

    // The row of the time format says what the format is.
    function timeFormatText(style: string, seconds: bool): string {
        if (style === "12h")
            return seconds ? qsTr("12-hour with seconds") : qsTr("12-hour");
        return seconds ? qsTr("24-hour with seconds") : qsTr("24-hour");
    }

    title: qsTr("Settings")
    scrollable: !board.dragging

    DelegateModel {
        id: errorTypeRows

        model: root.settings.errorTypes
        delegate: Item {}
    }

    DelegateModel {
        id: templateRows

        model: root.settings.templates
        delegate: Item {}
    }

    DelegateModel {
        id: locationRows

        model: root.settings.locations
        delegate: Item {}
    }

    Connections {
        target: root.settings

        function onWriteFailed(operation: string, text: string): void {
            root.show(text);
        }
    }

    Connections {
        target: root.settings.boardLayout

        // After the board has taken the change, so the tile it arrives on has its new type.
        function onDataChanged(): void {
            Qt.callLater(root.followSelected);
        }
    }

    Banner {
        objectName: "settingsMessage"
        width: parent.width
        visible: root.message.length > 0
        title: root.message
        tone: Banner.Error
        onCloseRequested: root.show("")
    }

    Banner {
        objectName: "settingsLoadProblem"
        width: parent.width
        visible: root.settings.loadProblem.length > 0
        title: qsTr("Your saved setup could not be used")
        message: root.settings.loadProblem
        tone: Banner.Warning
        closable: false
    }

    SettingsSkeleton {
        objectName: "settingsSkeleton"
        width: parent.width
        visible: root.loading
    }

    Column {
        id: loaded

        width: parent.width
        visible: !root.loading
        spacing: Theme.space.sectionGap

        Text {
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            color: Theme.colour.textSecondary
            font: Theme.font.sectionLabel.font
            text: qsTr("Your board").toLocaleUpperCase()
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        MiniBoard {
            id: board

            width: parent.width
            model: root.settings.boardLayout
            onMoveRequested: (fromPosition, toPosition) => root.move(fromPosition, toPosition)
            onReplaceRequested: position => root.replaceRequested(position, board.tileItem(position - 1).typeId)
        }

        Text {
            objectName: "settingsBoardHint"
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: board.selectedPosition > 0 ? qsTr("Tap a tile to select it. Hold and drag to move it, or use the buttons. Each type keeps its colour.") : qsTr("Hold and drag a tile to move it. Tap a tile to swap in a different error type. Each type keeps its colour.")
            wrapMode: Text.WordWrap
        }

        GroupedList {
            width: parent.width

            ListRow {
                objectName: "settingsErrorTypes"
                text: qsTr("Error types")
                value: errorTypeRows.count.toString()
                showChevron: true
                onActivated: root.errorTypesRequested()
            }

            ListRow {
                objectName: "settingsTemplates"
                text: qsTr("Note templates")
                value: templateRows.count.toString()
                showChevron: true
                onActivated: root.templatesRequested()
            }

            ListRow {
                objectName: "settingsLocations"
                text: qsTr("Locations")
                value: locationRows.count.toString()
                showChevron: true
                onActivated: root.locationsRequested()
            }

            ListRow {
                objectName: "settingsFacilitator"
                text: qsTr("Facilitator")
                value: root.settings.facilitator
                showChevron: true
                onActivated: root.facilitatorRequested()
            }

            ListRow {
                objectName: "settingsTimeFormat"
                text: qsTr("Time format")
                value: root.timeFormatText(root.settings.timeStyle, root.settings.timeSeconds)
                showChevron: true
                onActivated: root.timeFormatRequested()
            }

            ListRow {
                objectName: "settingsImportExport"
                text: qsTr("Import or export setup")
                showChevron: true
                onActivated: root.importExportRequested()
            }
        }

        GroupedList {
            title: qsTr("In the shared log")
            width: parent.width

            ListRow {
                objectName: "settingsUnitPrefix"
                text: qsTr("Unit prefix")
                value: root.settings.unitPrefix
                showChevron: true
                onActivated: root.unitPrefixRequested()
            }

            SettingsSwitchRow {
                objectName: "settingsTotals"
                text: qsTr("Include a totals line")
                description: qsTr("Adds the number of each error type under the log.")
                checked: root.settings.includeTotals
                onToggled: wanted => root.show(root.settings.setIncludeTotals(wanted))
            }
        }
    }
}
