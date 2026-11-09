// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQml
import ErrorJournalingApp

// The Log screen (Stage 4, package 4.7): the main board, the screen the facilitator lives in. From the top: the header card,
// the note composer with its template chips, the "Last" bar and the board of 12 tiles. The undo toast floats over the
// bottom of the board, the write-failed and recovery banners float over the header card (so nothing moves when one
// appears), and the template value prompt is a small sheet. docs/design/screens/Main, LogBanner, TemplateValue,
// WriteFailed and Recovery are the reference.
//
// The screen shows what the view models say and holds no count, no line and no rule of its own. The press path is its one
// job: a tile's `pressed` fires the impact haptic and then calls `session.recordErrorEntry`, from that handler and nothing
// else (Stage 5 decision 1); a VoiceOver activation of a tile sends the same pair of signals, so it takes the same two
// calls. The count, the badge, the "Last" bar and the toast change only after the commit, because the models change them
// only then.
//
// A tile is logged by a double-tap, not by one tap (4.17a): `Tile` sends `pressed` at the touch of the second tap, so the haptic
// and the press scale are at that touch and the latency budget (NFR-03) is counted from it. One tap logs nothing: the tile sends
// `singleTapped` once the double-tap window (`Theme.motion.doubleTapMs`, 300 ms) has gone by, and the screen shows the hint
// "Double-tap to log" (`TapHint`, at most once in `Theme.motion.tapHintIntervalMs`). The Other tile is a navigation and opens
// its list on a single tap.
//
// A hold on a tile (BRD-07, NOTE-06; `Tile.holdMs`, 500 ms) logs it with a note, in one of two ways that the composer decides.
// The hold records the entry (the tile sends `pressed` at the hold when no double-tap has already; a double-tap that goes on
// into a hold has recorded at its second touch, and the hold adds to that entry, so the entry is recorded once) and then:
//   - a draft is typed: `session.attachDraft` puts it on the entry's line and clears the composer, and the undo toast takes the
//     entry and the note away together and gives the draft back ("Mute Delay logged at 10:30 with note");
//   - the composer is empty: the Edit entry sheet opens on the new entry with the cursor in the note; Save attaches what is
//     typed and Cancel leaves the entry as it was.
// A hold waits for `entryRecorded` and then does the same, which is always the case when the hold itself recorded the entry. A
// slide off and a release before 500 ms are not holds (the release is a tap and counts towards a double-tap). With a draft, each
// tile has a second VoiceOver element, "Log <name> with note"; with none, the Edit sheet of Review is the way to a note (the sheet
// that a hold opens is the same).
//
// The success haptic of a saved note is not fired here. `PlatformFeedbackSink::note_saved` fires it from C++ after the
// commit, and a second one from this screen would be a double tap on the hand. The screen calls the bridge for the impact
// and for telling the haptic policy that the board is on screen (`setBoardVisible`).
//
// What it is given (decision 3):
//   session    LogSession: the header, `draft`, `state`, `recoveryNotice`, `recordErrorEntry`, `attachDraft`, `addNote`,
//              `addTemplatedNote`
//   board      BoardModel: the 12 tiles
//   undo       UndoController: the toast
//   timeline   TimelineModel: `lastLine` and `entryCount` for the "Last" bar, `rowOf` for a held entry, and the Edit entry sheet
//   settings   SettingsModel: `templates` for the chips, `unitPrefix` for the card's prompt and `previewTemplateLine` for the
//              "In the log" line of the value sheet
//   bridge     QmlBridge: `hapticImpact` and `setBoardVisible`
//   picker     ErrorPickerModel, for the error types of the Edit entry sheet that a hold opens; null where that sheet is not used
//
// What it asks the shell for, by signal:
//   todaysDetailsRequested()   the header card was tapped (Today's details, 4.9)
//   reviewRequested()          the "Last" bar was tapped, or "Check the log in Review" on the recovery banner (4.10)
//   otherRequested()           the Other tile was tapped, once (the picker, 4.8); nothing was recorded
//   boardSetupRequested(position)  an empty tile was tapped (its position, 1 to 11), or "Add error types" of the empty
//                              board (0): the board setup in Settings, with the picker open for that position when there
//                              is one. Nothing is recorded.
//
// A board with no error type on it (a setup made by hand) says so over the tiles (docs/design/screens/LogEmptyBoard):
// "Your board is empty" and "Add error types". The empty tiles stay buttons under it.
//
// The screen fills the window. `bottomClearance` is the height of the tab bar at the foot of it, which the toast stays
// above; the shell sets it if the screen does not extend behind the tab bar. The template value sheet is a child of
// `sheetLayer`, which the shell sets to its SheetHost layer so that the sheet covers the tab bar as well (a sheet under
// the disabled screen would be disabled too).
Item {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // log column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.logColumn)

    required property var session
    required property var board
    required property var undo
    required property var timeline
    required property var settings
    required property var bridge
    property var picker: null

    property real bottomClearance: Theme.size.tabBarHeight
    property Item sheetLayer: root

    // The safe-area insets, as SafeFrame has them: a test overrides them.
    property alias overrideInsets: frame.overrideInsets
    property alias overrideTop: frame.overrideTop
    property alias overrideBottom: frame.overrideBottom
    property alias overrideLeft: frame.overrideLeft
    property alias overrideRight: frame.overrideRight

    // LogSession::State::LOADING. The enum is not a QML type, so its first value is named here.
    readonly property int stateLoading: 0
    // The start-up read of the open log has not finished: every part draws its shape only and takes no touch.
    readonly property bool loading: session.state === stateLoading
    // What stands in for an empty line of the header card while the log loads. It is never seen (the card draws shapes). Each
    // line is one short mark in the font of the card, so that none of them wraps and the card has the height it will have once
    // the values are in.
    readonly property string loadingMark: "-"

    // The words of the banner for a write that failed, empty when there is none. A tap on an error type that could not
    // be saved says which type (`session.recordFailed`); any other failed write (a note, an undo) has the general title.
    property string failureTitle: qsTr("Not saved")
    property string failureMessage: ""
    // The recovery notice was closed (or acted on). The notice itself is fixed for the run.
    property bool recoveryDismissed: false
    // The template whose value the sheet is asking for, or null.
    property var activeTemplate: null
    // The icon of each template's chip, by templateId: "music" and "speech" as the setup file names them, and the speech
    // bubble for a template that names none or one the app does not draw (the model gives "note").
    property var chipIcons: ({})

    signal todaysDetailsRequested
    signal reviewRequested
    signal otherRequested
    signal boardSetupRequested(int position)

    // A note has been typed and not added: a hold on a tile logs with it.
    readonly property bool hasDraft: session.draft.trim().length > 0
    // The entry that the latest request of each tile recorded, by row; empty when it recorded nothing.
    property var pressedEntries: ({})
    // Holds that came before their commit: entry id to "draft" or "sheet", how the hold was decided.
    property var waitingHolds: ({})
    // The Edit entry sheet and the time sheet over it, made when the first hold needs them.
    readonly property var editor: (editLoader.item as LogEditSheets)?.editor ?? null

    // The impact, then the entry, when a tile asks for it: the second tap of a double-tap, or a hold. Other records nothing and
    // fires nothing: it opens the picker on its tap.
    function pressTile(row: int, typeId: string, isOther: bool): void {
        if (isOther)
            return;
        bridge.hapticImpact();
        pressedEntries[row] = session.recordErrorEntry(typeId);
    }

    // The finger stayed on a tile (or VoiceOver chose "Log with note"). The tile has asked for the entry already (`pressTile`,
    // in the same call for a hold); how it takes a note is decided now, by whether there is a draft, and done when the entry has
    // been committed.
    function holdTile(row: int): void {
        const entryId = pressedEntries[row] ?? "";
        if (entryId.length === 0)
            return;
        const how = hasDraft ? "draft" : "sheet";
        if (timeline.rowOf(entryId) >= 0)
            finishHold(entryId, how);
        else
            waitingHolds[entryId] = how;
    }

    function finishHold(entryId: string, how: string): void {
        if (how === "draft") {
            session.attachDraft(entryId);
        } else {
            editLoader.active = true;
            editor.editNote(entryId);
        }
    }

    // One tap on a tile logged nothing: say what logs it. The card is made at the first hint.
    function showTapHint(): void {
        hintLoader.active = true;
        (hintLoader.item as TapHint)?.show();
    }

    function updateChipIcons(): void {
        const icons = {};
        for (let i = 0; i < templateRows.count; ++i) {
            const row = templateRows.objectAt(i) as TemplateRow;
            if (row !== null)
                icons[row.templateId] = row.icon === "music" ? "music" : "speech";
        }
        chipIcons = icons;
    }

    // Shows the banner for a failure that the screen has no better words for.
    function fail(message: string): void {
        failureTitle = qsTr("Not saved");
        failureMessage = message;
    }

    function templateFor(templateId: string): var {
        for (let i = 0; i < templateRows.count; ++i) {
            const row = templateRows.objectAt(i) as TemplateRow;
            if (row.templateId === templateId)
                return row;
        }
        return null;
    }

    function chooseTemplate(templateId: string): void {
        const row = templateFor(templateId);
        if (row === null)
            return;
        activeTemplate = row;
        templateSheet.begin();
    }

    // Adds the note, or says inline why not. The session refuses an empty value and a note that does not fit.
    function addTemplated(value: string): void {
        if (session.addTemplatedNote(activeTemplate.templateId, value))
            templateSheet.close();
        else
            templateSheet.errorText = value.trim().length === 0 ? qsTr("Type something to add.") : qsTr("That note could not be added.");
    }

    onFailureMessageChanged: {
        if (failureMessage.length > 0)
            Accessible.announce(qsTr("%1. %2").arg(failureTitle).arg(failureMessage));
    }
    onVisibleChanged: bridge.setBoardVisible(visible)
    Component.onCompleted: {
        bridge.setBoardVisible(visible);
        if (session.recoveryNotice.length > 0)
            Accessible.announce(qsTr("Your log was repaired. %1").arg(session.recoveryNotice));
    }

    implicitWidth: Theme.frame.width
    implicitHeight: Theme.frame.height

    Connections {
        target: root.session

        // A tap on an error type that could not be saved: the banner names the type. The session also reports the same
        // failure as a write failure of "recordError", which is ignored here so that two banners do not appear.
        function onRecordFailed(errorTypeId: string, errorTypeName: string, title: string, message: string): void {
            root.failureTitle = title;
            root.failureMessage = message;
        }
        function onWriteFailed(operation: string, message: string): void {
            if (operation !== "recordError")
                root.fail(message);
        }
        // The next entry that is saved clears the old complaint.
        function onEntryRecorded(entryId: string): void {
            root.failureMessage = "";
            const how = root.waitingHolds[entryId];
            if (how !== undefined) {
                delete root.waitingHolds[entryId];
                root.finishHold(entryId, how);
            }
        }
    }

    Connections {
        target: root.undo

        function onWriteFailed(operation: string, message: string): void {
            root.fail(message);
        }
    }

    // One row of the template model, as the value prompt needs it.
    component TemplateRow: QtObject {
        required property string templateId
        required property string name
        required property string valuePrompt
        required property string outputPattern
        required property string icon
    }

    // The template rows, to find the one a chip names. The chips themselves are the composer's.
    Instantiator {
        id: templateRows

        model: root.settings.templates
        delegate: TemplateRow {}
        onObjectAdded: root.updateChipIcons()
        onObjectRemoved: root.updateChipIcons()
    }

    SafeFrame {
        id: frame

        anchors.fill: parent

        HeaderCard {
            id: header

            objectName: "logHeaderCard"
            x: root.side
            width: parent.width - 2 * root.side
            unitText: root.session.unitText
            locationText: root.session.locationText
            versionText: root.session.versionText
            dateText: root.session.dateText
            facilitatorText: root.session.facilitator
            missingFields: root.session.missingFields
            unitPrompt: root.loading ? root.loadingMark : qsTr("%1 —").arg(root.settings.unitPrefix)
            locationPrompt: root.loading ? root.loadingMark : qsTr("Location not set")
            versionPrompt: root.loading ? root.loadingMark : qsTr("v —")
            facilitatorPrompt: root.loading ? root.loadingMark : qsTr("Facilitator not set")
            skeleton: root.loading
            onClicked: root.todaysDetailsRequested()
        }

        // The messages float over the header card, which they follow in reading order; the board never moves for them.
        Column {
            id: banners

            objectName: "logBanners"
            x: root.side
            width: parent.width - 2 * root.side
            z: 1
            spacing: Theme.space.tileGap

            Banner {
                id: failureBanner

                objectName: "logWriteFailedBanner"
                width: parent.width
                visible: root.failureMessage.length > 0
                tone: Banner.Error
                floating: true
                title: root.failureTitle
                message: root.failureMessage
                maximumHeight: root.height / 2
                onCloseRequested: root.failureMessage = ""
            }

            Banner {
                id: recoveryBanner

                objectName: "logRecoveryBanner"
                width: parent.width
                visible: root.session.recoveryNotice.length > 0 && !root.recoveryDismissed
                tone: Banner.Warning
                floating: true
                title: qsTr("Your log was repaired")
                message: root.session.recoveryNotice
                actionText: qsTr("Check the log in Review")
                maximumHeight: root.height / 2
                onActionTriggered: {
                    root.recoveryDismissed = true;
                    root.reviewRequested();
                }
                onCloseRequested: root.recoveryDismissed = true
            }
        }

        NoteComposer {
            id: composer

            objectName: "logComposer"
            x: root.side
            width: parent.width - 2 * root.side
            anchors.top: header.bottom
            anchors.topMargin: Theme.space.sectionGap
            draft: root.session.draft
            chips: root.settings.templates
            chipIcons: root.chipIcons
            skeleton: root.loading
            onTextEdited: text => root.session.draft = text
            onSubmit: text => root.session.addNote(text)
            onChipChosen: templateId => root.chooseTemplate(templateId)
        }

        LastEntryBar {
            id: lastBar

            objectName: "logLastBar"
            x: root.side
            width: parent.width - 2 * root.side
            anchors.top: composer.bottom
            anchors.topMargin: Theme.space.sectionGap
            lastLine: root.timeline.lastLine
            entryCount: root.timeline.entryCount
            skeleton: root.loading
            onTapped: root.reviewRequested()
        }

        // The empty board: a card over the first three rows of tiles (LogEmptyBoard), with the way to the board setup.
        // It is made only while the board is empty, so the board as it is used pays one item for it (perf.scene). It
        // comes before the board in the tree, so a screen reader reads it first, and the tiles under it are skipped.
        Loader {
            id: emptyBoardLoader

            // The first three rows: the board without its last row and the gap above it.
            readonly property real rowsHeight: grid.boardHeight - Theme.size.tileHeight - Theme.space.tileGap

            x: grid.x
            y: grid.y
            z: 1
            width: grid.width
            height: rowsHeight
            active: root.board.boardEmpty && !root.loading
            sourceComponent: emptyBoardCard
        }

        Component {
            id: emptyBoardCard

            Item {
                id: emptyBoard

                objectName: "logEmptyBoard"
                width: emptyBoardLoader.width
                height: Math.max(emptyBoardLoader.height, emptyColumn.implicitHeight + 2 * emptyColumn.x)
                Accessible.role: Accessible.Grouping
                Accessible.name: qsTr("Your board is empty")

                Shadow {
                    anchors.fill: emptyCard
                    radius: emptyCard.radius
                    token: Theme.shadow.toast
                }

                Rectangle {
                    id: emptyCard

                    anchors.fill: parent
                    radius: Theme.radius.headerCard
                    color: Theme.colour.surface

                    // A barrier, as a sheet's scrim is: a touch on the card does not reach the empty tile under it.
                    MultiPointTouchArea {
                        anchors.fill: parent
                        mouseEnabled: true
                    }
                }

                Column {
                    id: emptyColumn

                    x: Theme.space.sectionGap + Theme.space.tileGap
                    y: (parent.height - implicitHeight) / 2
                    width: parent.width - 2 * x
                    spacing: Theme.space.tileGap

                    Icon {
                        anchors.horizontalCenter: parent.horizontalCenter
                        size: Theme.size.badge * 4 / 3
                        kind: "board"
                        colour: Theme.colour.primaryText
                    }

                    Text {
                        objectName: "logEmptyBoardTitle"
                        width: parent.width
                        topPadding: Theme.border.hairline * 4
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.colour.textPrimary
                        font: Theme.font.sheetTitle.font
                        text: qsTr("Your board is empty")
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.colour.textSecondary
                        font: Theme.font.bodySmall.font
                        text: qsTr("Add the error types you log. Each one becomes a tile here.")
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    // 16 pt above the button: the column's gap on each side of this.
                    Item {
                        width: parent.width
                        height: 0
                    }

                    PrimaryButton {
                        objectName: "logAddErrorTypes"
                        width: parent.width
                        text: qsTr("Add error types")
                        onClicked: root.boardSetupRequested(0)
                    }
                }
            }
        }

        BoardGrid {
            id: grid

            objectName: "logBoard"
            x: root.side
            width: parent.width - 2 * root.side
            anchors.top: lastBar.bottom
            anchors.topMargin: Theme.space.sectionGap
            model: root.board
            skeleton: root.loading
            coveredTiles: emptyBoardLoader.active ? Theme.size.boardColumns * (Theme.size.boardRows - 1) : 0
            noteAction: root.hasDraft
            onTilePressed: (row, typeId, isOther) => root.pressTile(row, typeId, isOther)
            onTileHeld: row => root.holdTile(row)
            onTileSingleTapped: root.showTapHint()
            onTileTapped: (row, typeId, isOther) => {
                if (isOther)
                    root.otherRequested();
            }
            onEmptyTapped: position => root.boardSetupRequested(position)
        }
    }

    UndoToast {
        id: toast

        objectName: "logToast"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: root.side
        anchors.rightMargin: root.side
        anchors.bottomMargin: root.bottomClearance + Theme.space.sectionGap
        active: root.undo.active
        text: root.undo.text
        undoing: root.undo.undoing
        generation: root.undo.generation
        windowMs: root.undo.windowMs
        onUndoRequested: root.undo.undo()
    }

    // The hint of a single tap, above the undo toast when that is open. It takes no touch. Made at the first hint.
    Loader {
        id: hintLoader

        objectName: "logTapHintLoader"
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: toast.bottom
        anchors.bottomMargin: toast.present ? toast.height + Theme.space.tileGap : 0
        active: false
        sourceComponent: TapHint {
            objectName: "logTapHint"
        }
    }

    // The Edit entry sheet of a hold on an empty composer. Made at the first hold, so that a board nobody holds does not pay for
    // it (perf.scene), and kept after. It fills the sheet layer as the sheets do, and tells the layer's host that a sheet is up.
    Loader {
        id: editLoader

        objectName: "logEditLoader"
        parent: root.sheetLayer
        anchors.fill: parent
        active: false
        readonly property bool isOpen: (item as LogEditSheets)?.isOpen ?? false
        sourceComponent: LogEditSheets {
            timeline: root.timeline
            picker: root.picker
            settings: root.settings
        }
    }

    TemplateValueSheet {
        id: templateSheet

        objectName: "logTemplateSheet"
        parent: root.sheetLayer
        templateName: root.activeTemplate ? root.activeTemplate.name : ""
        valuePrompt: root.activeTemplate ? root.activeTemplate.valuePrompt : ""
        outputPattern: root.activeTemplate ? root.activeTemplate.outputPattern : ""
        previewText: root.activeTemplate ? root.settings.previewTemplateLine(root.activeTemplate.outputPattern, templateSheet.value) : ""
        onAddRequested: value => root.addTemplated(value)
    }
}
