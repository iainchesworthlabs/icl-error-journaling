// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The History screen (Stage 4, package 4.11; DAY-04, DAY-05): the archived logs, newest first, in one card for each date, each
// row with its unit and site, how many entries it has and whether it has been shared. A row opens the log in the Review screen
// of 4.10, pushed on a PageStack and bound to `history.reviewModel`; a swipe on a row offers Share and Delete; a delete is
// confirmed in an Alert that names the date, the unit and site and the entries, and is then made by the model.
//
//   history     the HistoryModel (rows, loading, reviewModel, refresh, openLog, closeLog, leave, requestDelete, confirmDelete)
//   share       the ShareService (available, shareLog, shareFinished)
//   picker      the ErrorPickerModel, for the Edit entry sheet of the past log
//   settings    the SettingsModel, for the time format of the Edit entry sheet
//
// The shell calls `shown()` when the tab is shown, which reads the list again, and `hidden()` when it is left, which goes back to
// the list and lets the open past log go (`history.leave()`). The screen shows what the model gives it: the dates, the unit and
// site and the shared time are the model's own strings, and the screen builds no line of a log. Nothing here fires a haptic; a
// share, a delete and the opening of a log are not taps on a tile.
//
// Review is given the date of the sharing from the row (`section`, "7 Nov 2026") and the time from `sharedAtText`, because the
// model formats the time of the share only. A log is opened by its row and closed by Review's back control, once the page has
// gone: the page keeps showing the log until its slide has ended.
//
// Layout (History, HistoryEmpty, DeleteLog): rows 60 pt high with the unit and site in 16/600 and "4 entries · v1.4.2" under
// them (the model's `detailText`), the shared state as a check and "Shared 17:42" or a ring and "Not shared" (or "Changed since sharing" for a log that was
// shared and has changed since), then a thin chevron; the separators run the full width of the card and the last row of a card has none. The delete alert names the date in
// full ("Thu 5 Nov 2026", the model's `longDateText`).
//
// The screen fills the window and keeps `bottomClearance` free at the bottom for the tab bar, which the shell draws. The shell
// sets `sheetLayer` to the layer of its own SheetHost, so that the delete alert, and the sheets of a past log's Review, cover
// the tab bar as well; without it they stay in the screen's own SheetHost.
Item {
    id: root

    required property var history
    required property var share
    required property var picker
    required property var settings
    // What the shell's tab bar covers at the bottom of the window.
    property real bottomClearance: Theme.size.tabBarHeight
    // The layer of the window's SheetHost, when the shell has one; null keeps the sheets in the screen's own.
    property Item sheetLayer: null

    // Something was said to a screen reader that is not on a control: a log was deleted, or Review's "Copied".
    signal announced(string message)

    // The safe-area insets. SafeFrame reads them; a test overrides them. They go to the Review page too.
    property bool overrideInsets: false
    property real overrideTop: 0
    property real overrideBottom: 0
    property real overrideLeft: 0
    property real overrideRight: 0

    // The page stack and the past log, for the shell and the tests.
    readonly property alias stack: pages
    readonly property bool reviewShown: pages.depth > 1
    readonly property bool empty: loadedOnce && list.count === 0

    // The row that was opened: Review is given its title and the time it was shared, which follow the row.
    property string openedLogId
    property string openedTitle
    property string openedSharedDate
    property string openedSharedTime
    property bool openedChangedSinceShare: false
    property string openedEntries
    // The timeline of the log that is open. It is kept after the log is closed, so that the page that is sliding away still
    // has a model to show.
    property var reviewTimeline: null
    // A log was asked for and has not arrived; a second tap on a row waits for it.
    property bool opening: false
    // Review's back control was used and the page is on its way out; the log is closed when it has gone.
    property bool closingReview: false
    // The list has been read since the screen was shown.
    property bool loadedOnce: false
    // The height of the title in the header of the list, for the empty state that sits below it.
    property real titleHeight: 0

    // What the banner says, if anything.
    property string noticeTitle
    property string noticeMessage
    // True from a tap on Share in the list until that share ends, so that only this screen's own share shows its failure.
    property bool shareStarted: false

    // The delete that is waiting for an answer: the token the model gave, and the words of the question.
    property string deleteToken
    property string deleteMessage

    implicitWidth: Theme.frame.width
    implicitHeight: Theme.frame.height

    // The shell shows the tab: read the list again.
    function shown(): void {
        root.noticeTitle = "";
        root.loadedOnce = false;
        root.history.refresh();
    }

    // The shell leaves the tab: back to the list at once, and the model lets the open log go.
    function hidden(): void {
        root.closingReview = false;
        root.opening = false;
        deleteAlert.close();
        root.deleteToken = "";
        if (pages.depth > 1)
            pages.pop(null, StackView.Immediate);
        root.history.leave();
    }

    function notify(title: string, message: string): void {
        root.noticeTitle = title;
        root.noticeMessage = message;
    }

    // "UNIT-27 · North Site", or the one of the two that the log has.
    function placeTitle(unit: string, location: string): string {
        if (unit.length > 0 && location.length > 0)
            return qsTr("%1 · %2").arg(unit).arg(location);
        if (unit.length > 0)
            return unit;
        if (location.length > 0)
            return location;
        return qsTr("No unit or site");
    }

    // "UNIT-27 at North Site", for a sentence.
    function placeSentence(unit: string, location: string): string {
        if (unit.length > 0 && location.length > 0)
            return qsTr("%1 at %2").arg(unit).arg(location);
        if (unit.length > 0)
            return unit;
        if (location.length > 0)
            return location;
        return qsTr("no unit or site");
    }

    function openRow(logId: string, title: string): void {
        if (root.opening)
            return;
        root.opening = true;
        root.openedLogId = logId;
        root.openedTitle = title;
        root.history.openLog(logId);
    }

    // Review's back control: slide the page away, then let the log go.
    function closeReview(): void {
        if (pages.depth <= 1)
            return;
        root.closingReview = true;
        pages.pop();
    }

    function shareRow(logId: string): void {
        root.noticeTitle = "";
        root.shareStarted = true;
        root.share.shareLog(logId);
    }

    // A swipe on Delete: the model gives a token for a log that may be deleted, and nothing for one that may not.
    function askDelete(logId: string, longDate: string, unit: string, location: string, entries: string): void {
        const token = root.history.requestDelete(logId);
        if (token.length === 0)
            return;
        root.deleteToken = token;
        root.deleteMessage = qsTr("%1, %2, %3. It is removed from this phone and can’t be brought back.").arg(longDate).arg(root.placeSentence(unit, location)).arg(entries);
        deleteAlert.open();
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colour.background
        Accessible.ignored: true
    }

    Connections {
        target: root.history

        function onReviewModelChanged(): void {
            const model = root.history.reviewModel;
            if (model !== null) {
                root.reviewTimeline = model;
                root.opening = false;
                root.closingReview = false;
                if (pages.depth === 1)
                    pages.push(reviewPage);
            } else if (pages.depth > 1 && !root.closingReview) {
                // The model let the log go (the shell, or a delete): there is nothing to show.
                pages.pop(null, StackView.Immediate);
            }
        }

        function onLoadingChanged(): void {
            if (!root.history.loading)
                root.loadedOnce = true;
        }

        function onRefreshFailed(message: string): void {
            root.loadedOnce = false;
            root.notify(qsTr("History could not be updated"), message);
        }

        function onLogOpenFailed(message: string): void {
            root.opening = false;
            root.notify(qsTr("The log could not be opened"), message);
        }

        function onDeleteFinished(logId: string, ok: bool): void {
            if (ok) {
                const message = qsTr("Log deleted");
                Accessible.announce(message);
                root.announced(message);
            } else {
                root.notify(qsTr("The log was not deleted"), qsTr("Nothing was changed. It is still in History."));
            }
        }
    }

    Connections {
        target: root.share

        function onShareFinished(logId: string, outcome: string): void {
            if (!root.shareStarted)
                return;
            root.shareStarted = false;
            if (outcome === "failed")
                root.notify(qsTr("The log was not shared"), qsTr("Nothing was changed. Try again, or open the log and copy it into a message."));
        }
    }

    SheetHost {
        anchors.fill: parent

        PageStack {
            id: pages

            anchors.fill: parent
            accessibleName: qsTr("History")
            initialItem: listPage
            onBusyChanged: {
                // The page has slid away: now the log can go.
                if (!pages.busy && pages.depth === 1 && root.closingReview) {
                    root.closingReview = false;
                    root.history.closeLog();
                }
            }
        }

        sheets: [
            Alert {
                id: deleteAlert

                objectName: "deleteAlert"
                title: qsTr("Delete this log?")
                message: root.deleteMessage
                icon: Alert.Trash
                destructive: true
                confirmText: qsTr("Delete log")
                cancelText: qsTr("Cancel")
                overrideInsets: root.overrideInsets
                overrideTop: root.overrideTop
                overrideBottom: root.overrideBottom
                overrideLeft: root.overrideLeft
                overrideRight: root.overrideRight
                onConfirmed: {
                    const token = root.deleteToken;
                    root.deleteToken = "";
                    if (!root.history.confirmDelete(token))
                        root.notify(qsTr("The log was not deleted"), qsTr("Nothing was changed. It is still in History."));
                }
                onCancelled: root.deleteToken = ""
            }
        ]
    }

    // The alert in the shell's layer, over the tab bar, when there is one.
    Binding {
        target: deleteAlert
        property: "parent"
        value: root.sheetLayer
        when: root.sheetLayer !== null
        restoreMode: Binding.RestoreNone
    }

    // The page below, which the stack adopts.
    Item {
        id: listPage

        objectName: "historyPage"

        SafeFrame {
            id: frame

            anchors.fill: parent
            overrideInsets: root.overrideInsets
            overrideTop: root.overrideTop
            overrideBottom: root.overrideBottom
            overrideLeft: root.overrideLeft
            overrideRight: root.overrideRight

            ListView {
                id: list

                objectName: "historyList"
                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                height: bottomEdge.y - y
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                bottomMargin: Theme.space.sectionGap
                model: root.history
                section.property: "section"
                section.criteria: ViewSection.FullString
                section.delegate: Item {
                    id: heading

                    required property string section

                    width: ListView.view.width
                    // 16 pt above the heading (8 under the card before it and 8 of its own margin) and 8 under it.
                    readonly property real above: Theme.space.sectionGap + Theme.space.tileGap / 2
                    height: headingText.implicitHeight + above + Theme.space.tileGap

                    Text {
                        id: headingText

                        objectName: "historySection"
                        x: Theme.space.tileGap / 2
                        y: heading.above
                        width: parent.width - 2 * x
                        color: Theme.colour.textSecondary
                        font: Theme.font.sectionLabel.font
                        text: heading.section.toLocaleUpperCase()
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.Heading
                        Accessible.name: heading.section
                    }
                }

                header: Column {
                    id: listHeader

                    width: list.width
                    spacing: Theme.space.tileGap
                    // 4 pt under the subtitle, before the first heading.
                    bottomPadding: Theme.space.tileGap / 2

                    Binding {
                        target: root
                        property: "titleHeight"
                        value: titleText.height
                        restoreMode: Binding.RestoreNone
                    }

                    Text {
                        id: titleText

                        objectName: "historyTitle"
                        width: parent.width
                        color: Theme.colour.textPrimary
                        font: Theme.font.largeTitle.font
                        text: qsTr("History")
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.Heading
                        Accessible.name: text
                    }

                    Text {
                        objectName: "historySubtitle"
                        visible: list.count > 0
                        width: parent.width
                        color: Theme.colour.textSecondary
                        font: Theme.font.bodySmall.font
                        text: qsTr("Past logs, newest first. Today’s log is on the Review tab.")
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }

                delegate: Item {
                    id: line

                    required property int index
                    required property string logId
                    required property string dateText
                    required property string section
                    required property string unitText
                    required property string locationText
                    required property int entryCount
                    required property string entriesText
                    required property string detailText
                    required property string longDateText
                    required property bool shared
                    required property bool changedSinceShare
                    required property string sharedAtText
                    required property string sharedDateText

                    readonly property bool firstInSection: ListView.previousSection !== line.section
                    readonly property bool lastInSection: ListView.nextSection !== line.section
                    readonly property bool isOpened: line.logId === root.openedLogId
                    readonly property string statusText: line.shared ? qsTr("Shared %1").arg(line.sharedAtText) : line.changedSinceShare ? qsTr("Changed since sharing") : qsTr("Not shared")
                    readonly property string rowName: qsTr("%1, %2, %3, %4").arg(line.dateText).arg(root.placeTitle(line.unitText, line.locationText)).arg(line.entriesText).arg(line.statusText)

                    objectName: "historyRow"
                    width: ListView.view.width
                    height: swipe.height

                    // What Review is told about the row that is open, kept when the row goes.
                    Binding {
                        target: root
                        property: "openedSharedDate"
                        value: line.sharedDateText
                        when: line.isOpened
                        restoreMode: Binding.RestoreNone
                    }

                    Binding {
                        target: root
                        property: "openedEntries"
                        value: line.entriesText
                        when: line.isOpened
                        restoreMode: Binding.RestoreNone
                    }

                    Binding {
                        target: root
                        property: "openedSharedTime"
                        value: line.sharedAtText
                        when: line.isOpened
                        restoreMode: Binding.RestoreNone
                    }

                    Binding {
                        target: root
                        property: "openedChangedSinceShare"
                        value: line.changedSinceShare
                        when: line.isOpened
                        restoreMode: Binding.RestoreNone
                    }

                    // The gap between the texts, the status and the chevron: 10 pt.
                    readonly property real gap: Theme.space.tileGap + Theme.border.hairline * 2

                    TextMetrics {
                        id: statusMetrics

                        font: Theme.font.caption.font
                        text: line.statusText
                    }

                    SwipeRow {
                        id: swipe

                        objectName: "historySwipe"
                        accessibleName: line.rowName
                        copyText: qsTr("Share")
                        showCopy: root.share.available
                        minimumHeight: Theme.size.rowHeightTall
                        // 8 pt above and below the two lines of text, as the design has them.
                        verticalPadding: Theme.space.tileGap
                        showDivider: !line.lastInSection
                        onActivated: root.openRow(line.logId, line.dateText)
                        onCopyRequested: root.shareRow(line.logId)
                        onDeleteRequested: root.askDelete(line.logId, line.longDateText, line.unitText, line.locationText, line.entriesText)

                        Item {
                            width: parent.width
                            height: Math.max(texts.height, statusRow.height)

                            Column {
                                id: texts

                                y: (parent.height - height) / 2
                                width: Math.max(parent.width * 0.4, parent.width - statusRow.width - chevron.width - 2 * line.gap)
                                spacing: Theme.border.hairline * 2

                                Text {
                                    objectName: "historyPlace"
                                    width: parent.width
                                    color: Theme.colour.textPrimary
                                    font: Theme.font.body.selectedFont
                                    text: root.placeTitle(line.unitText, line.locationText)
                                    wrapMode: Text.WordWrap
                                    Accessible.ignored: true
                                }

                                Text {
                                    objectName: "historyDetail"
                                    width: parent.width
                                    color: Theme.colour.textSecondary
                                    font: Theme.font.caption.font
                                    text: line.detailText
                                    wrapMode: Text.WordWrap
                                    Accessible.ignored: true
                                }
                            }

                            Row {
                                id: statusRow

                                x: parent.width - width - chevron.width - line.gap
                                y: (parent.height - height) / 2
                                spacing: Theme.border.hairline * 4

                                Icon {
                                    objectName: "historyStatusIcon"
                                    anchors.verticalCenter: parent.verticalCenter
                                    kind: line.shared ? "check" : "ring"
                                    size: Theme.size.badge * 2 / 3
                                    colour: line.shared ? Theme.colour.primaryText : Theme.colour.textSecondary
                                }

                                Text {
                                    id: statusLabel

                                    objectName: "historyStatus"
                                    anchors.verticalCenter: parent.verticalCenter
                                    width: Math.min(statusMetrics.advanceWidth, line.width * 0.3)
                                    horizontalAlignment: Text.AlignRight
                                    color: line.shared ? Theme.colour.textSecondary : Theme.colour.textPrimary
                                    font: Theme.font.caption.font
                                    text: line.statusText
                                    wrapMode: Text.WordWrap
                                    Accessible.ignored: true
                                }
                            }

                            Icon {
                                id: chevron

                                objectName: "historyChevron"
                                x: parent.width - width
                                y: (parent.height - height) / 2
                                kind: "chevronRight"
                                size: Theme.size.badge * 7 / 12
                                colour: Theme.colour.borderStrong
                            }
                        }
                    }

                    // The corners of the card of the date.
                    HistoryCorner {
                        visible: line.firstInSection
                        atTop: true
                        atLeft: true
                    }
                    HistoryCorner {
                        visible: line.firstInSection
                        x: parent.width - width
                        atTop: true
                        atLeft: false
                    }
                    HistoryCorner {
                        visible: line.lastInSection
                        y: parent.height - height
                        atTop: false
                        atLeft: true
                    }
                    HistoryCorner {
                        visible: line.lastInSection
                        x: parent.width - width
                        y: parent.height - height
                        atTop: false
                        atLeft: false
                    }
                }
            }

            // No past logs, or the first read of them.
            Column {
                id: placeholder

                objectName: "historyPlaceholder"
                visible: root.empty || (root.history.loading && list.count === 0)
                // 24 pt of padding inside the margin, and 150 pt under the title (HistoryEmpty).
                x: 2 * Theme.space.screenMargin + Theme.space.tileGap
                y: list.y + root.titleHeight + Theme.space.tileGap + 150 // literal-ok: the offset of HistoryEmpty.dc.html, which has no token
                width: parent.width - 2 * x
                spacing: Theme.space.sectionGap

                Rectangle {
                    objectName: "historyEmptyIcon"
                    visible: root.empty
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Theme.size.listRowHeight * 4 / 3
                    height: width
                    radius: width / 2
                    color: Theme.colour.primaryTint
                    Accessible.ignored: true

                    Icon {
                        anchors.centerIn: parent
                        kind: "calendar"
                        size: Theme.size.badge * 5 / 4
                        colour: Theme.colour.primaryText
                    }
                }

                Text {
                    objectName: "historyEmptyTitle"
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.colour.textPrimary
                    font: Theme.font.headerUnit.font
                    text: root.empty ? qsTr("No past logs yet") : qsTr("Loading the logs…")
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.empty ? qsTr("No past logs yet. When you start a new day, yesterday’s log moves here, where you can open it, copy it or share it again.") : text
                }

                Text {
                    objectName: "historyEmptyText"
                    visible: root.empty
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    color: Theme.colour.textSecondary
                    font: Theme.font.alertMessage.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.alertMessage.linePitch
                    text: qsTr("When you start a new day, yesterday’s log moves here, where you can open it, copy it or share it again.")
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
                }
            }

            // Where the bottom of the page is: the top of the tab bar.
            Item {
                id: bottomEdge

                y: parent.height - Math.max(0, root.bottomClearance - frame.bottomInset)
                width: 0
                height: 0
            }

            // A failure over the bottom of the list.
            Banner {
                id: notice

                objectName: "historyNotice"
                visible: root.noticeTitle.length > 0
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: bottomEdge.top
                anchors.leftMargin: Theme.space.screenMargin
                anchors.rightMargin: Theme.space.screenMargin
                anchors.bottomMargin: Theme.space.tileGap
                title: root.noticeTitle
                message: root.noticeMessage
                tone: Banner.Error
                maximumHeight: bottomEdge.y - Theme.space.sectionGap
                onCloseRequested: root.noticeTitle = ""
            }
        }
    }

    // The past log, which the stack pushes when the model has loaded it.
    Component {
        id: reviewPage

        Review {
            timeline: root.reviewTimeline
            share: root.share
            picker: root.picker
            settings: root.settings
            title: root.openedTitle
            pastLog: true
            logId: root.openedLogId
            backText: qsTr("History")
            entriesText: root.openedEntries
            sharedDateText: root.openedSharedDate
            sharedTimeText: root.openedSharedTime
            changedSinceShare: root.openedChangedSinceShare
            sheetLayer: root.sheetLayer
            bottomClearance: root.bottomClearance
            overrideInsets: root.overrideInsets
            overrideTop: root.overrideTop
            overrideBottom: root.overrideBottom
            overrideLeft: root.overrideLeft
            overrideRight: root.overrideRight
            onBackRequested: root.closeReview()
            onAnnounced: message => root.announced(message)
        }
    }
}
