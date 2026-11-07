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
// job: a tile's `pressed` fires the impact haptic and then calls `session.recordError`, from that handler and nothing
// else (Stage 5 decision 1); a VoiceOver activation of a tile sends the same pair of signals, so it takes the same two
// calls. The count, the badge, the "Last" bar and the toast change only after the commit, because the models change them
// only then.
//
// The success haptic of a saved note is not fired here. `PlatformFeedbackSink::note_saved` fires it from C++ after the
// commit, and a second one from this screen would be a double tap on the hand. The screen calls the bridge for the impact
// and for telling the haptic policy that the board is on screen (`setBoardVisible`).
//
// What it is given (decision 3):
//   session    LogSession: the header, `draft`, `state`, `recoveryNotice`, `recordError`, `addNote`, `addTemplatedNote`
//   board      BoardModel: the 12 tiles
//   undo       UndoController: the toast
//   timeline   TimelineModel: `lastLine` and `entryCount` for the "Last" bar
//   settings   SettingsModel: `templates` for the chips and `unitPrefix` for the card's prompt
//   bridge     QmlBridge: `hapticImpact` and `setBoardVisible`
//
// What it asks the shell for, by signal:
//   todaysDetailsRequested()   the header card was tapped (Today's details, 4.9)
//   reviewRequested()          the "Last" bar was tapped, or "Check the log in Review" on the recovery banner (4.10)
//   otherRequested()           the Other tile was tapped (the picker, 4.8); nothing was recorded
//
// The screen fills the window. `bottomClearance` is the height of the tab bar at the foot of it, which the toast stays
// above; the shell sets it if the screen does not extend behind the tab bar. The template value sheet is a child of
// `sheetLayer`, which the shell sets to its SheetHost layer so that the sheet covers the tab bar as well (a sheet under
// the disabled screen would be disabled too).
Item {
    id: root

    required property var session
    required property var board
    required property var undo
    required property var timeline
    required property var settings
    required property var bridge

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

    // The words of the banner for a write that failed (the session's and the undo's), empty when there is none.
    property string failureMessage: ""
    // The recovery notice was closed (or acted on). The notice itself is fixed for the run.
    property bool recoveryDismissed: false
    // The template whose value the sheet is asking for, or null.
    property var activeTemplate: null

    signal todaysDetailsRequested
    signal reviewRequested
    signal otherRequested

    // The impact on touch-down, then the entry. Other records nothing and fires nothing: it opens the picker on its tap.
    function pressTile(typeId: string, isOther: bool): void {
        if (isOther)
            return;
        bridge.hapticImpact();
        session.recordError(typeId);
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
            Accessible.announce(qsTr("Not saved. %1").arg(failureMessage));
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

        function onWriteFailed(operation: string, message: string): void {
            root.failureMessage = message;
        }
        // The next entry that is saved clears the old complaint.
        function onEntryRecorded(entryId: string): void {
            root.failureMessage = "";
        }
    }

    Connections {
        target: root.undo

        function onWriteFailed(operation: string, message: string): void {
            root.failureMessage = message;
        }
    }

    // One row of the template model, as the value prompt needs it.
    component TemplateRow: QtObject {
        required property string templateId
        required property string name
        required property string valuePrompt
        required property string outputPattern
    }

    // The template rows, to find the one a chip names. The chips themselves are the composer's.
    Instantiator {
        id: templateRows

        model: root.settings.templates
        delegate: TemplateRow {}
    }

    SafeFrame {
        id: frame

        anchors.fill: parent

        HeaderCard {
            id: header

            objectName: "logHeaderCard"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
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
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            z: 1
            spacing: Theme.space.tileGap

            Banner {
                id: failureBanner

                objectName: "logWriteFailedBanner"
                width: parent.width
                visible: root.failureMessage.length > 0
                tone: Banner.Error
                title: qsTr("Not saved")
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
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            anchors.top: header.bottom
            anchors.topMargin: Theme.space.sectionGap
            draft: root.session.draft
            chips: root.settings.templates
            skeleton: root.loading
            onTextEdited: text => root.session.draft = text
            onSubmit: text => root.session.addNote(text)
            onChipChosen: templateId => root.chooseTemplate(templateId)
        }

        LastEntryBar {
            id: lastBar

            objectName: "logLastBar"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            anchors.top: composer.bottom
            anchors.topMargin: Theme.space.sectionGap
            lastLine: root.timeline.lastLine
            entryCount: root.timeline.entryCount
            skeleton: root.loading
            onTapped: root.reviewRequested()
        }

        BoardGrid {
            id: grid

            objectName: "logBoard"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            anchors.top: lastBar.bottom
            anchors.topMargin: Theme.space.sectionGap
            model: root.board
            skeleton: root.loading
            onTilePressed: (row, typeId, isOther) => root.pressTile(typeId, isOther)
            onTileTapped: (row, typeId, isOther) => {
                if (isOther)
                    root.otherRequested();
            }
        }
    }

    UndoToast {
        id: toast

        objectName: "logToast"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space.screenMargin
        anchors.rightMargin: Theme.space.screenMargin
        anchors.bottomMargin: root.bottomClearance + Theme.space.sectionGap
        active: root.undo.active
        text: root.undo.text
        undoing: root.undo.undoing
        generation: root.undo.generation
        windowMs: root.undo.windowMs
        onUndoRequested: root.undo.undo()
    }

    TemplateValueSheet {
        id: templateSheet

        objectName: "logTemplateSheet"
        parent: root.sheetLayer
        templateName: root.activeTemplate ? root.activeTemplate.name : ""
        valuePrompt: root.activeTemplate ? root.activeTemplate.valuePrompt : ""
        outputPattern: root.activeTemplate ? root.activeTemplate.outputPattern : ""
        onAddRequested: value => root.addTemplated(value)
    }
}
