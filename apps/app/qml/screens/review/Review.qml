// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The Review screen (Stage 4, package 4.10; REV-01, REV-03, REV-04, REV-05): the header block and the timeline of a log in
// the words of the export, in black and white only (only the tiles of the Log screen are coloured), with Copy and Delete on a
// swipe, Copy-all in the bar at the top, "Share log" at the bottom and the Edit entry sheet over it.
//
// One file serves the open log and a past log of History. It takes any TimelineModel: with `pastLog` it has a back control,
// the date of the sharing and "Share log again", and shares the log `logId` instead of the open one.
//
//   timeline    the TimelineModel shown (rows, headerText, titleText, copyLine, copyAll, deleteEntry)
//   share       the ShareService (available, sharing, shareOpenLog, shareLog, shareFinished)
//   picker      the ErrorPickerModel, for the list of error types of the Edit entry sheet
//   settings    the SettingsModel, for the time format of the time tumbler
//
// The screen shows nothing that a model does not give it and builds no line: the rows are `timeText`, `prefixText` and
// `restText` (`lineText` when a row has no prefix), the header block is `headerText`. A delete is `timeline.deleteEntry`; the toast that offers Undo comes from UndoController and
// is the shell's, so the screen has no toast of its own. Sharing never fires a haptic. A copy is confirmed by a short
// "Copied" that is announced to a screen reader.
//
// The screen fills the window. It keeps `bottomClearance` free at the bottom for the tab bar, which the shell draws.
Item {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // reading column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.readingColumn)

    required property var timeline
    required property var share
    required property var picker
    required property var settings
    // The big title: "Today’s log", or the date of a past log.
    property string title: qsTr("Today’s log")
    // A past log of History: back control, "<n> entries · Shared <date> at <time>" and "Share log again".
    property bool pastLog: false
    // The id of the past log, for `share.shareLog`.
    property string logId
    // The name of the screen the back control returns to.
    property string backText: qsTr("History")
    // When the past log was last shared. Both empty = never shared, and nothing is said.
    property string sharedDateText
    property string sharedTimeText
    // The past log has changed since that share (an edit, a delete or a restore made here, in History's own list): it is no
    // longer the log that was sent.
    property bool changedSinceShare: false
    // "12 entries": the first words of the line under a past log's title (HistoryModel's `entriesText`).
    property string entriesText: timeline.entryCount === 1 ? qsTr("1 entry") : qsTr("%1 entries").arg(timeline.entryCount)
    // What the shell's tab bar covers at the bottom of the window.
    property real bottomClearance: Theme.size.tabBarHeight

    // The back control was activated (past log only).
    signal backRequested
    // The header block was tapped (open log only): the shell shows Today's details.
    signal headerRequested
    // Something was said to a screen reader that is not on a control: the "Copied" confirmation.
    signal announced(string message)

    // The Edit entry sheet and the time sheet over it, for the shell and the tests.
    property alias editor: entryEditor
    property alias timeSheet: entryTimeSheet
    // The layer of the window's SheetHost, when the shell has one: the two sheets are then put in it, so that they cover the tab
    // bar too (a sheet in this screen's own SheetHost is under it). Null keeps them in the screen's own.
    property Item sheetLayer: null

    // The safe-area insets. SafeFrame reads them; a test overrides them.
    property alias overrideInsets: frame.overrideInsets
    property alias overrideTop: frame.overrideTop
    property alias overrideBottom: frame.overrideBottom
    property alias overrideLeft: frame.overrideLeft
    property alias overrideRight: frame.overrideRight

    readonly property bool empty: timeline.entryCount === 0
    readonly property bool shareShown: share.available
    readonly property bool hasShared: sharedDateText.length > 0 && sharedTimeText.length > 0
    // The line under a past log's title, as PastLogReview has it: "12 entries · Shared 5 Nov at 16:58", or the entries alone
    // when the log was never shared. A log that has changed since its share does not read as fully shared: "12 entries ·
    // Changed since sharing on 5 Nov at 16:58".
    readonly property string pastSubtitle: !hasShared ? entriesText : changedSinceShare ? qsTr("%1 · Changed since sharing on %2 at %3").arg(entriesText).arg(sharedDateText).arg(sharedTimeText) : qsTr("%1 · Shared %2 at %3").arg(entriesText).arg(sharedDateText).arg(sharedTimeText)
    // The cards of the header block and the rows have a hairline in dark mode and on a past log (PastLogReview), and none on
    // the open log in light mode (Review).
    readonly property bool outlined: Theme.dark || pastLog
    // The font roles of a row: the time and the line share the pitch of rowText so that their lines align.
    readonly property real rowPitch: Theme.font.rowText.linePitch
    // The widest time of the log's format, so the lines start at one place.
    readonly property real timeColumnWidth: Math.max(Theme.size.minTouchTarget, widestA.advanceWidth, widestB.advanceWidth)
    // At the largest text sizes the time would leave the line a sliver, so it goes on a line of its own above it.
    readonly property bool stackTimes: timeColumnWidth > (width - 2 * (root.side + Theme.space.rowPadding)) * 0.3

    // What the banner says, if anything: a share or an edit that failed.
    property string noticeTitle
    property string noticeMessage
    // The "Copied" confirmation; empty when there is none.
    property string copiedText
    // True from the tap on Share until the share ends, so that only this screen's own share shows its failure.
    property bool shareStarted: false

    implicitWidth: Theme.frame.width
    implicitHeight: Theme.frame.height

    function openEdit(entryId: string): void {
        entryEditor.edit(entryId);
    }

    // A time of day in the user's format. The key is an argument only so that a binding on this runs again when the
    // format changes (SettingsModel::timeFormatKey).
    function timeAt(seconds: int, formatKey: string): string {
        return timeline.formatLocalSeconds(seconds);
    }

    function confirmCopied(message: string): void {
        copiedText = message;
        copiedTimer.restart();
        Accessible.announce(message);
        announced(message);
    }

    function copyAll(): void {
        if (!empty && timeline.copyAll())
            confirmCopied(qsTr("Copied the whole log"));
    }

    function copyRow(row: int): void {
        if (timeline.copyLine(row))
            confirmCopied(qsTr("Copied the line"));
    }

    // A row for StyledText: the prefix in the secondary colour, and both parts with the characters that it reads as markup
    // written out.
    function styledLine(prefix: string, rest: string): string {
        const safe = value => value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return "<font color=\"" + Theme.colour.reviewTextSecondary + "\">" + safe(prefix) + "</font> " + safe(rest);
    }

    function startShare(): void {
        noticeTitle = "";
        shareStarted = true;
        // The iPad's popover points at the button; a phone ignores it.
        const at = shareButton.mapToItem(null, 0, 0);
        share.setAnchor(at.x, at.y, shareButton.width, shareButton.height);
        if (pastLog)
            share.shareLog(logId);
        else
            share.shareOpenLog();
    }

    Timer {
        id: copiedTimer

        objectName: "copiedTimer"
        interval: Theme.motion.undoToastVisibleMs / 2
        onTriggered: root.copiedText = ""
    }

    // The sheet of an edit failing later than its Save: the model says so here.
    Connections {
        target: root.timeline

        function onWriteFailed(operation: string, message: string): void {
            root.noticeTitle = qsTr("The change was not saved");
            root.noticeMessage = message;
        }
    }

    Connections {
        target: root.share

        function onShareFinished(logId: string, outcome: string): void {
            if (!root.shareStarted)
                return;
            root.shareStarted = false;
            if (outcome === "failed") {
                root.noticeTitle = qsTr("The log was not shared");
                root.noticeMessage = qsTr("Nothing was changed. Try again, or copy the log and paste it into a message.");
            }
        }
    }

    TextMetrics {
        id: widestA

        font: Theme.font.timeColumn.font
        text: root.timeAt(0, root.settings.timeFormatKey)
    }

    // 12:58 in the afternoon: the widest time of a 12-hour format.
    TextMetrics {
        id: widestB

        font: Theme.font.timeColumn.font
        text: root.timeAt(46680, root.settings.timeFormatKey)
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colour.reviewBackground
        Accessible.ignored: true
    }

    SheetHost {
        anchors.fill: parent

        SafeFrame {
            id: frame

            anchors.fill: parent

            // ---- the bar: back or the summary, and Copy-all ----

            Item {
                id: bar

                x: root.side
                width: parent.width - 2 * root.side
                height: Math.max(Theme.size.minTouchTarget, summary.implicitHeight, backButton.height)

                Item {
                    id: backButton

                    objectName: "reviewBack"
                    visible: root.pastLog
                    width: Math.max(Theme.size.minTouchTarget, backRow.width)
                    height: Math.max(Theme.size.minTouchTarget, backRow.height)
                    activeFocusOnTab: visible
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Back to %1").arg(root.backText)
                    Accessible.focusable: true
                    Accessible.ignored: !root.pastLog
                    Accessible.onPressAction: root.backRequested()

                    TapHandler {
                        enabled: backButton.visible
                        onTapped: root.backRequested()
                    }

                    Row {
                        id: backRow

                        anchors.verticalCenter: parent.verticalCenter
                        // The chevron is 20 pt, 2 pt from its label (PastLogReview).
                        spacing: Theme.border.hairline * 2

                        Icon {
                            kind: "chevronLeft"
                            size: Theme.size.badge * 5 / 6
                            strokeWidth: Theme.border.hairline * 2.2
                            colour: Theme.colour.primaryText
                            anchors.verticalCenter: parent.verticalCenter
                        }

                        Text {
                            // The name wraps rather than push Copy-all out of the window at the largest text sizes.
                            width: Math.min(implicitWidth, bar.width - Theme.size.badge - 2 * Theme.size.minTouchTarget)
                            color: Theme.colour.primaryText
                            font: Theme.font.navAction.font
                            text: root.backText
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                    Keys.onReturnPressed: root.backRequested()
                    Keys.onSpacePressed: root.backRequested()
                }

                Text {
                    id: summary

                    objectName: "reviewSummary"
                    visible: !root.pastLog
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - copyButton.width - Theme.space.sectionGap
                    color: Theme.colour.reviewTextSecondary
                    font: Theme.font.bodySmall.font
                    text: root.timeline.titleText
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Item {
                    id: copyButton

                    objectName: "reviewCopyAll"
                    // The icon and its word, so the button says what it does (Stage 7, F11). At the largest text sizes the word
                    // would take the room of the title, and the icon is alone, as before.
                    readonly property bool labelShown: copyLabel.implicitWidth + copyIcon.width + Theme.space.tileGap < bar.width * 0.45
                    x: parent.width - width
                    anchors.verticalCenter: parent.verticalCenter
                    width: Math.max(Theme.size.minTouchTarget, labelShown ? copyIcon.width + Theme.space.tileGap + copyLabel.implicitWidth : copyIcon.width)
                    height: Theme.size.minTouchTarget
                    enabled: !root.empty
                    opacity: enabled ? 1 : 0.4
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Copy whole log")
                    Accessible.focusable: true
                    Accessible.onPressAction: root.copyAll()

                    TapHandler {
                        onTapped: root.copyAll()
                    }

                    Icon {
                        id: copyIcon

                        objectName: "reviewCopyIcon"
                        x: copyButton.labelShown ? 0 : (parent.width - width) / 2
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "copy"
                        size: Theme.size.badge * 11 / 12
                        colour: Theme.colour.primaryText
                    }

                    Text {
                        id: copyLabel

                        objectName: "reviewCopyLabel"
                        x: copyIcon.width + Theme.space.tileGap
                        anchors.verticalCenter: parent.verticalCenter
                        visible: copyButton.labelShown
                        color: Theme.colour.primaryText
                        font: Theme.font.bodySmall.selectedFont
                        text: qsTr("Copy log")
                        Accessible.ignored: true
                    }
                    Keys.onReturnPressed: root.copyAll()
                    Keys.onSpacePressed: root.copyAll()
                }
            }

            // Where the bottom of the page is: the top of the Share button, or the top of the tab bar when there is none.
            Item {
                id: bottomEdge

                y: shareButton.visible ? shareButton.y : parent.height - Math.max(0, root.bottomClearance - frame.bottomInset)
                width: 0
                height: 0
            }

            // ---- the timeline ----

            ListView {
                id: list

                objectName: "reviewList"
                x: root.side
                y: bar.height
                width: parent.width - 2 * root.side
                // The last rows stay clear of the banner and "Copied" that float over the bottom of the page: the list ends above
                // them, so a row is never under something a screen reader reads after it.
                height: bottomEdge.y - Theme.space.tileGap - y - (floating.height > 0 ? floating.height + Theme.space.tileGap : 0)
                clip: true
                reuseItems: true
                // No rows beyond the edges: one under the banner would be read by a screen reader before the banner.
                cacheBuffer: 0
                boundsBehavior: Flickable.StopAtBounds
                bottomMargin: Theme.space.sectionGap
                model: root.timeline

                header: Column {
                    width: list.width
                    spacing: Theme.space.sectionGap
                    // The title sits 8 pt under the bar: the mock-ups have a 12 pt gap and a -4 pt margin on the title.
                    topPadding: Theme.space.tileGap
                    bottomPadding: Theme.space.sectionGap

                    Text {
                        objectName: "reviewTitle"
                        width: parent.width
                        color: Theme.colour.reviewText
                        font: Theme.font.largeTitle.font
                        text: root.title
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    // A past log says how many entries it has and when it was shared, on one line.
                    Text {
                        objectName: "reviewSubtitle"
                        visible: root.pastLog
                        width: parent.width
                        height: visible ? implicitHeight : 0
                        color: Theme.colour.reviewTextSecondary
                        font: Theme.font.bodySmall.font
                        text: root.pastSubtitle
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }

                    // The header block: the start values, as the export prints them.
                    Item {
                        id: headerCard

                        // The hairline of the card takes a point of its own, as a CSS border does.
                        readonly property real edge: root.outlined ? Theme.border.hairline : 0

                        objectName: "reviewHeader"
                        width: parent.width
                        height: headerText.implicitHeight + 2 * (Theme.space.sectionGap + headerCard.edge)
                        activeFocusOnTab: !root.pastLog
                        Accessible.role: root.pastLog ? Accessible.StaticText : Accessible.Button
                        Accessible.name: headerText.text
                        Accessible.description: root.pastLog ? "" : qsTr("Opens today’s details")
                        Accessible.focusable: !root.pastLog
                        Accessible.onPressAction: {
                            if (!root.pastLog)
                                root.headerRequested();
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radius.groupedList
                            color: headerTap.pressed ? Theme.colour.primaryTint : Theme.colour.reviewSurface
                            border.width: headerCard.edge
                            border.color: Theme.colour.reviewDivider
                        }

                        Text {
                            id: headerText

                            objectName: "reviewHeaderText"
                            x: Theme.space.rowPadding + headerCard.edge
                            // Qt puts a fixed line at the top of its box and CSS in the middle: half the leading moves it.
                            y: Theme.space.sectionGap + headerCard.edge + (Theme.font.headerBlock.linePitch - Theme.font.headerBlock.linePitch / Theme.font.headerBlock.lineRatio) / 2
                            width: parent.width - 2 * (Theme.space.rowPadding + headerCard.edge)
                            color: Theme.colour.reviewText
                            font: Theme.font.headerBlock.font
                            lineHeightMode: Text.FixedHeight
                            lineHeight: Theme.font.headerBlock.linePitch
                            text: root.timeline.headerText
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }

                        TapHandler {
                            id: headerTap

                            enabled: !root.pastLog
                            onTapped: root.headerRequested()
                        }
                        Keys.onReturnPressed: {
                            if (!root.pastLog)
                                root.headerRequested();
                        }
                    }
                }

                // The empty log says so, in the place of the rows; a log with entries says how to use them.
                footer: Item {
                    readonly property bool hintShown: !root.empty && !root.pastLog

                    width: list.width
                    height: root.empty ? emptyCard.height : hintShown ? hint.implicitHeight : 0
                    visible: root.empty || hintShown

                    Text {
                        id: hint

                        objectName: "reviewHint"
                        visible: parent.hintShown
                        width: parent.width
                        topPadding: Theme.space.sectionGap
                        leftPadding: Theme.space.tileGap / 2
                        rightPadding: Theme.space.tileGap / 2
                        color: Theme.colour.reviewTextSecondary
                        font: Theme.font.footnote.font
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.font.footnote.linePitch
                        text: qsTr("Tap a line to edit it. Swipe left for Copy and Delete.")
                        wrapMode: Text.WordWrap
                        // For a finger: a screen reader has each row as a button, and Copy and Delete as buttons under it.
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: emptyCard

                        visible: root.empty

                        objectName: "reviewEmpty"
                        width: parent.width
                        height: emptyColumn.height + 2 * Theme.space.sectionGap
                        radius: Theme.radius.groupedList
                        color: Theme.colour.reviewSurface
                        border.width: root.outlined ? Theme.border.hairline : 0
                        border.color: Theme.colour.reviewDivider
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("No entries yet. The errors you log are listed here.")

                        Column {
                            id: emptyColumn

                            x: Theme.space.rowPadding
                            y: Theme.space.sectionGap
                            width: parent.width - 2 * Theme.space.rowPadding

                            Text {
                                width: parent.width
                                color: Theme.colour.reviewText
                                font: Theme.font.sheetTitle.font
                                text: qsTr("No entries yet")
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }

                            Text {
                                width: parent.width
                                color: Theme.colour.reviewTextSecondary
                                font: Theme.font.bodySmall.font
                                text: qsTr("The errors you log are listed here.")
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }
                    }
                }

                delegate: Item {
                    id: line

                    required property int index
                    required property string entryId
                    required property string timeText
                    required property string lineText
                    required property string prefixText
                    required property string restText
                    required property string fullLine

                    readonly property bool first: index === 0
                    readonly property bool last: index === list.count - 1

                    objectName: "reviewRow"
                    width: ListView.view.width
                    height: swipe.height
                    // A row in the pool of the list is not on the page: it is hidden, so that a screen reader and the checks
                    // do not find it under the banner or past the edge.
                    ListView.onPooled: line.visible = false
                    ListView.onReused: line.visible = true

                    SwipeRow {
                        id: swipe

                        objectName: "reviewSwipe"
                        accessibleName: line.fullLine.length > 0 ? line.fullLine : line.lineText
                        surface: Theme.colour.reviewSurface
                        dividerColour: Theme.colour.reviewDivider
                        showDivider: !line.last
                        onActivated: root.openEdit(line.entryId)
                        onCopyRequested: root.copyRow(line.index)
                        onDeleteRequested: root.timeline.deleteEntry(line.entryId)

                        Item {
                            width: parent.width
                            height: root.stackTimes ? timeLabel.height + lineLabel.implicitHeight : Math.max(timeLabel.height, lineLabel.implicitHeight)

                            Text {
                                id: timeLabel

                                objectName: "reviewTime"
                                width: root.stackTimes ? parent.width : root.timeColumnWidth
                                height: root.rowPitch
                                color: Theme.colour.reviewTextSecondary
                                font: Theme.font.timeColumn.font
                                lineHeightMode: Text.FixedHeight
                                lineHeight: root.rowPitch
                                verticalAlignment: Text.AlignVCenter
                                text: line.timeText
                                Accessible.ignored: true
                            }

                            Text {
                                id: lineLabel

                                objectName: "reviewLine"
                                x: root.stackTimes ? 0 : timeLabel.width + Theme.space.sectionGap
                                y: root.stackTimes ? timeLabel.height : 0
                                width: parent.width - x
                                color: Theme.colour.reviewText
                                font: Theme.font.rowText.font
                                lineHeightMode: Text.FixedHeight
                                lineHeight: root.rowPitch
                                // The prefix ("T|", "(Free Text)") is in the secondary colour, as the mock-ups have it.
                                textFormat: line.prefixText.length > 0 ? Text.StyledText : Text.PlainText
                                text: line.prefixText.length > 0 ? root.styledLine(line.prefixText, line.restText) : line.lineText
                                wrapMode: Text.WordWrap
                                Accessible.ignored: true
                            }
                        }
                    }

                    // The card's outline: the sides of every row, and the top of the first and the bottom of the last.
                    Rectangle {
                        visible: root.outlined
                        width: Theme.border.hairline
                        height: parent.height
                        color: Theme.colour.reviewDivider
                        Accessible.ignored: true
                    }
                    Rectangle {
                        visible: root.outlined
                        x: parent.width - width
                        width: Theme.border.hairline
                        height: parent.height
                        color: Theme.colour.reviewDivider
                        Accessible.ignored: true
                    }
                    Rectangle {
                        visible: root.outlined && line.first
                        width: parent.width
                        height: Theme.border.hairline
                        color: Theme.colour.reviewDivider
                        Accessible.ignored: true
                    }
                    Rectangle {
                        visible: root.outlined && line.last
                        y: parent.height - height
                        width: parent.width
                        height: Theme.border.hairline
                        color: Theme.colour.reviewDivider
                        Accessible.ignored: true
                    }
                    ReviewCorner {
                        outlined: root.outlined
                        visible: line.first
                        atTop: true
                        atLeft: true
                    }
                    ReviewCorner {
                        outlined: root.outlined
                        visible: line.first
                        x: parent.width - width
                        atTop: true
                        atLeft: false
                    }
                    ReviewCorner {
                        outlined: root.outlined
                        visible: line.last
                        y: parent.height - height
                        atTop: false
                        atLeft: true
                    }
                    ReviewCorner {
                        outlined: root.outlined
                        visible: line.last
                        x: parent.width - width
                        y: parent.height - height
                        atTop: false
                        atLeft: false
                    }
                }
            }

            // ---- what floats over the bottom of the list: a failure, and "Copied" ----

            Column {
                id: floating

                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: bottomEdge.top
                anchors.leftMargin: root.side
                anchors.rightMargin: root.side
                anchors.bottomMargin: Theme.space.tileGap
                spacing: Theme.space.tileGap

                Banner {
                    objectName: "reviewNotice"
                    visible: root.noticeTitle.length > 0
                    width: parent.width
                    title: root.noticeTitle
                    message: root.noticeMessage
                    tone: Banner.Error
                    onCloseRequested: root.noticeTitle = ""
                }

                Item {
                    width: parent.width
                    height: root.copiedText.length > 0 ? copiedPill.height : 0
                    visible: root.copiedText.length > 0

                    Rectangle {
                        id: copiedPill

                        objectName: "reviewCopied"
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: Math.min(parent.width, copiedLabel.implicitWidth + 2 * Theme.space.rowPadding)
                        height: copiedLabel.implicitHeight + 2 * Theme.space.tileGap
                        radius: Theme.radius.toast
                        color: Theme.colour.toastBackground
                        Accessible.role: Accessible.StaticText
                        Accessible.name: root.copiedText

                        Text {
                            id: copiedLabel

                            anchors.centerIn: parent
                            width: parent.width - 2 * Theme.space.rowPadding
                            horizontalAlignment: Text.AlignHCenter
                            color: Theme.colour.toastText
                            font: Theme.font.bodySmall.font
                            text: root.copiedText
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                }
            }

            PrimaryButton {
                id: shareButton

                objectName: "reviewShare"
                visible: root.shareShown
                enabled: !root.share.sharing && !root.empty
                x: root.side
                y: parent.height - height - Math.max(0, root.bottomClearance - frame.bottomInset) - Theme.space.sectionGap
                width: parent.width - 2 * root.side
                iconKind: "share"
                text: root.pastLog ? qsTr("Share log again") : qsTr("Share log")
                onClicked: root.startShare()
            }
        }

        sheets: [
            EditEntry {
                id: entryEditor

                timeline: root.timeline
                picker: root.picker
                settings: root.settings
            },
            // The time of the entry, over the Edit entry sheet: declared after it, so that it stacks over it.
            EditTimeSheet {
                id: entryTimeSheet

                edit: entryEditor
                timeline: root.timeline
                settings: root.settings
            }
        ]
    }

    // Puts the sheets in the shell's layer, the Edit entry sheet first, so that the time sheet is over it.
    function placeSheets(): void {
        if (sheetLayer === null)
            return;
        entryEditor.parent = sheetLayer;
        entryTimeSheet.parent = sheetLayer;
    }

    onSheetLayerChanged: placeSheets()
    Component.onCompleted: placeSheets()
}
