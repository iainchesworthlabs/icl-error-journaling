// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The Settings list (Stage 4, package 4.12; PRF-01 to 07, 10): the miniature board on top of it and, under the board, the
// rows. The board is MiniBoard over `settings.boardLayout`; a move it asks for goes to `settings.moveBoardTile` and the
// choice of a type for a tile is the screen's (`replaceRequested`), which opens the picker and calls
// `settings.assignBoardTile`. Change error type in the panel of a selected tile and a tap on an empty tile both ask for it;
// an empty tile has no type, so its `typeId` is empty. The rows that open an editor of the lists emit a signal and the shell shows the editor
// (package 4.13); the others open a page of this screen.
//
// A refusal of the model, a write that failed and a setup that could not be read are shown inline, above the board.
// While the setup is being read (`settings.loading`) the page shows its skeleton and offers nothing.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var settings
    // The foot of the list: the version and platform text, as a plain caption. Empty hides it. The build's facts, the licence and
    // the timing line (five taps on the version) are on the About page, which the last row opens.
    property string aboutText
    // Whether the About row is there: the About model is given. The screen sets it from `about`.
    property bool aboutAvailable: false

    signal errorTypesRequested
    signal templatesRequested
    signal locationsRequested
    signal unitEnvironmentsRequested
    signal facilitatorRequested
    signal unitPrefixRequested
    signal errorPrefixRequested
    signal timeFormatRequested
    signal importExportRequested
    signal aboutRequested
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
    // BoardSettings: 8 pt above the title and 16 pt between it and "YOUR BOARD" (8 of them are the NavBar's own).
    topGap: Theme.space.tileGap
    bodyGap: Theme.space.tileGap

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

        // The spacing is the design's own, item by item (BoardSettings, BoardLayoutSelected): the gaps are set below.
        width: parent.width
        visible: !root.loading
        spacing: 0

        Text {
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            color: Theme.colour.textSecondary
            font: Theme.font.sectionLabel.font
            text: qsTr("Your board").toLocaleUpperCase()
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Item {
            width: parent.width
            height: Theme.space.tileGap
        }

        MiniBoard {
            id: board

            width: parent.width
            model: root.settings.boardLayout
            onMoveRequested: (fromPosition, toPosition) => root.move(fromPosition, toPosition)
            onReplaceRequested: position => root.replaceRequested(position, board.tileItem(position - 1).typeId)
            onAddRequested: position => root.replaceRequested(position, "")
        }

        // 10 pt under the resting board and 8 pt under the panel of a selected tile, as the two mock-ups have it.
        Item {
            width: parent.width
            height: board.selectedPosition > 0 ? Theme.space.tileGap : Theme.space.tileGap * 5 / 4
        }

        Text {
            objectName: "settingsBoardHint"
            x: Theme.space.tileGap / 2
            width: parent.width - 2 * x
            color: Theme.colour.textSecondary
            font: Theme.font.footnote.font
            lineHeightMode: Text.FixedHeight
            lineHeight: Theme.font.footnote.linePitch
            text: board.selectedPosition > 0 ? qsTr("Tap a tile to select it. Hold and drag to move it, or use the buttons. Each type keeps its colour.") : qsTr("Hold and drag a tile to move it. Tap a tile to change its error type, or + to add one. Each type keeps its colour.")
            wrapMode: Text.WordWrap
        }

        // 18 pt under the hint of the resting board and 8 pt under the hint of a selected tile.
        Item {
            width: parent.width
            height: board.selectedPosition > 0 ? Theme.space.tileGap : Theme.space.sectionGap * 3 / 2
        }

        GroupedList {
            width: parent.width

            ListRow {
                objectName: "settingsErrorTypes"
                text: qsTr("Error types")
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.errorTypesRequested()
            }

            ListRow {
                objectName: "settingsTemplates"
                text: qsTr("Note templates")
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.templatesRequested()
            }

            ListRow {
                objectName: "settingsLocations"
                text: qsTr("Locations")
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.locationsRequested()
            }

            ListRow {
                objectName: "settingsUnitEnvironments"
                text: qsTr("Unit environments")
                value: root.settings.unitEnvironments.length
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.unitEnvironmentsRequested()
            }

            ListRow {
                objectName: "settingsFacilitator"
                text: qsTr("Facilitator")
                value: root.settings.facilitator
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.facilitatorRequested()
            }

            // The unit prefix and the totals line are on the list and nowhere else (Stage 7, F4): they are not behind the
            // facilitator, and no page repeats them.
            ListRow {
                objectName: "settingsUnitPrefixRow"
                text: qsTr("Unit prefix")
                value: root.settings.unitPrefix
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.unitPrefixRequested()
            }

            SettingsSwitchRow {
                objectName: "settingsTotalsRow"
                text: qsTr("Include a totals line")
                checked: root.settings.includeTotals
                onToggled: wanted => root.show(root.settings.setIncludeTotals(wanted))
            }

            ListRow {
                objectName: "settingsTimeFormat"
                text: qsTr("Time format")
                value: root.timeFormatText(root.settings.timeStyle, root.settings.timeSeconds)
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.timeFormatRequested()
            }

            ListRow {
                objectName: "settingsErrorPrefixRow"
                text: qsTr("Error prefix")
                value: root.settings.errorPrefix.length > 0 ? root.settings.errorPrefix : qsTr("None")
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.errorPrefixRequested()
            }

            ListRow {
                objectName: "settingsImportExport"
                text: qsTr("Import or export setup")
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.importExportRequested()
            }
        }

        Item {
            width: parent.width
            height: Theme.space.sectionGap
            visible: root.aboutAvailable
        }

        GroupedList {
            width: parent.width
            visible: root.aboutAvailable

            ListRow {
                objectName: "settingsAboutRow"
                text: qsTr("About CS Bingo Board")
                minimumHeight: Theme.size.rowHeightCompact
                showChevron: true
                onActivated: root.aboutRequested()
            }
        }
    }

    SettingsAbout {
        objectName: "settingsAboutFooter"
        aboutText: root.aboutText
    }
}
