// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The log pane of the Log screen in a Wide window (Stage 8, package 8.5; docs/design/screens/Main.ipad-landscape, T8-10):
// today's log beside the board, so that what was logged is in view while it is logged. It is a card in the colours of
// Review (black and white only: the tiles are the only colour) with the title, the number of entries and Copy-all, the rows
// of the log in the words of the export, and "Share log" at its foot.
//
// It is the Review screen's list in a card: the same TimelineModel, the same rows (a tap opens Edit entry, a swipe gives Copy
// and Delete, and the prefix of a line is in the secondary colour), the same Copy-all, the same Share, and the same notices.
// Its Share button points the iPad's popover at itself. It has no state of its own that a rotation could lose: the Log
// screen makes it when the window becomes Wide and drops it when it stops being, and the model is the log.
//
//   timeline    the TimelineModel (rows, entryCount, formatLocalSeconds, copyLine, copyAll, deleteEntry)
//   settings    the SettingsModel, for the time format
//   share       the ShareService, or null: no Share button then
//
// The Log screen puts the Undo toast over the foot of the list, above Share log, in the room the list keeps free for it
// (`toastRoom`), so that no entry is under the toast and the pane does not change size when the toast comes and goes.
Item {
    id: root

    required property var timeline
    required property var settings
    property var share: null

    // The room the list keeps free above the Share button for the Undo toast: its height and the gap above the button.
    readonly property real toastRoom: Theme.size.toastHeight + Theme.space.sectionGap
    // How far the foot of the card is under the top of the Share button: the padding, the button and the padding.
    readonly property real footerHeight: root.shareShown ? Theme.size.primaryButtonHeight + 2 * Theme.space.rowPadding : 0
    // From the foot of the card to the foot of the toast: the Share button's area and the gap above it.
    readonly property real toastBottom: footerHeight + Theme.space.sectionGap

    readonly property bool empty: timeline.entryCount === 0
    readonly property bool shareShown: share !== null && share.available
    // The outline of a card of Review in dark mode.
    readonly property bool outlined: Theme.dark

    // What the banner says, if anything: a share or an edit that failed (as on Review).
    property string noticeTitle
    property string noticeMessage
    // The "Copied" confirmation; empty when there is none.
    property string copiedText
    // True from the tap on Share until the share ends, so that only this pane's own share shows its failure.
    property bool shareStarted: false
    // The widest time of the log's format, so the lines start at one place.
    readonly property real timeColumnWidth: Math.max(Theme.size.minTouchTarget, widestA.advanceWidth, widestB.advanceWidth)
    readonly property real rowPitch: Theme.font.rowText.linePitch

    // A row was tapped: the Log screen opens Edit entry on it.
    signal editRequested(string entryId)

    // A time of day in the user's format. The key is an argument only so that a binding on this runs again when the format
    // changes (SettingsModel::timeFormatKey), as on Review.
    function timeAt(seconds: int, formatKey: string): string {
        return timeline.formatLocalSeconds(seconds);
    }

    function copyAll(): void {
        if (!empty && timeline.copyAll())
            confirmCopied(qsTr("Copied the whole log"));
    }

    function copyRow(row: int): void {
        if (timeline.copyLine(row))
            confirmCopied(qsTr("Copied the line"));
    }

    function confirmCopied(message: string): void {
        copiedText = message;
        copiedTimer.restart();
        Accessible.announce(message);
    }

    // The same markup as Review's rows: the prefix of an error line ("T|") in the secondary colour, and both parts with the
    // characters that it reads as markup written out.
    function styledLine(prefix: string, rest: string): string {
        const safe = value => value.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
        return "<font color=\"" + Theme.colour.reviewTextSecondary + "\">" + safe(prefix) + "</font> " + safe(rest);
    }

    function startShare(): void {
        noticeTitle = "";
        shareStarted = true;
        // The popover points at this button; the phone ignores it.
        const at = shareButton.mapToItem(null, 0, 0);
        share.setAnchor(at.x, at.y, shareButton.width, shareButton.height);
        share.shareOpenLog();
    }

    // The newest entry is in view when one is added, as it is on Review.
    function showNewest(): void {
        Qt.callLater(() => list.positionViewAtEnd());
    }

    Accessible.role: Accessible.Grouping
    Accessible.name: qsTr("Today’s log")

    Timer {
        id: copiedTimer

        interval: Theme.motion.undoToastVisibleMs / 2
        onTriggered: root.copiedText = ""
    }

    Connections {
        target: root.timeline

        function onWriteFailed(operation: string, message: string): void {
            root.noticeTitle = qsTr("The change was not saved");
            root.noticeMessage = message;
        }
    }

    Connections {
        target: root.share
        enabled: root.share !== null

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
        id: card

        objectName: "logPaneCard"
        anchors.fill: parent
        radius: Theme.radius.card
        color: Theme.colour.reviewSurface
        border.width: root.outlined ? Theme.border.hairline : 0
        border.color: Theme.colour.reviewDivider
        Accessible.ignored: true
    }

    // ---- the title: today's log, how many entries, Copy-all ----

    Item {
        id: titleRow

        objectName: "logPaneTitle"
        x: Theme.space.rowPadding
        width: parent.width - 2 * x
        height: Theme.size.rowHeightForm

        Text {
            id: title

            anchors.verticalCenter: parent.verticalCenter
            color: Theme.colour.reviewText
            font: Theme.font.sheetTitle.font
            text: qsTr("Today’s log")
            Accessible.role: Accessible.Heading
            Accessible.name: text
        }

        Text {
            objectName: "logPaneCount"
            anchors.verticalCenter: parent.verticalCenter
            x: parent.width - copyButton.width - Theme.space.tileGap - width
            color: Theme.colour.reviewTextSecondary
            font: Theme.font.bodySmall.font
            text: root.timeline.entryCount === 1 ? qsTr("1 entry") : qsTr("%1 entries").arg(root.timeline.entryCount)
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Item {
            id: copyButton

            objectName: "logPaneCopyAll"
            x: parent.width - width + Theme.space.tileGap
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.size.minTouchTarget
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
                anchors.centerIn: parent
                kind: "copy"
                size: Theme.size.badge * 11 / 12
                colour: Theme.colour.primaryText
            }
            Keys.onReturnPressed: root.copyAll()
            Keys.onSpacePressed: root.copyAll()
        }
    }

    Rectangle {
        id: rule

        y: titleRow.height
        width: parent.width
        height: Theme.border.hairline
        color: Theme.colour.reviewDivider
        Accessible.ignored: true
    }

    // ---- the rows ----

    ListView {
        id: list

        objectName: "logPaneList"
        y: rule.y + rule.height
        width: parent.width
        // The list ends above what floats over the foot of the pane: the room for the Undo toast, or a notice that is taller
        // (a share that failed), and the Share button's area. A row is never under the toast, and none under a banner that
        // a screen reader reads after it.
        height: parent.height - y - root.footerHeight - Math.max(root.toastRoom, floating.height > 0 ? floating.height + Theme.space.sectionGap : 0)
        clip: true
        reuseItems: true
        cacheBuffer: 0
        boundsBehavior: Flickable.StopAtBounds
        model: root.timeline
        onCountChanged: root.showNewest()
        onHeightChanged: root.showNewest()

        delegate: Item {
            id: line

            required property int index
            required property string entryId
            required property string timeText
            required property string lineText
            required property string prefixText
            required property string restText
            required property string fullLine

            readonly property bool last: index === list.count - 1
            // At the largest text sizes the time would leave the line a sliver, so it goes on a line of its own above it.
            readonly property bool stackTimes: root.timeColumnWidth > (width - 2 * Theme.space.rowPadding) * 0.3

            objectName: "logPaneRow"
            width: ListView.view.width
            height: swipe.height
            ListView.onPooled: line.visible = false
            ListView.onReused: line.visible = true

            SwipeRow {
                id: swipe

                objectName: "logPaneSwipe"
                accessibleName: line.fullLine.length > 0 ? line.fullLine : line.lineText
                surface: Theme.colour.reviewSurface
                dividerColour: Theme.colour.reviewDivider
                showDivider: !line.last
                onActivated: root.editRequested(line.entryId)
                onCopyRequested: root.copyRow(line.index)
                onDeleteRequested: root.timeline.deleteEntry(line.entryId)

                Item {
                    width: parent.width
                    height: line.stackTimes ? timeLabel.height + lineLabel.implicitHeight : Math.max(timeLabel.height, lineLabel.implicitHeight)

                    Text {
                        id: timeLabel

                        objectName: "logPaneTime"
                        width: line.stackTimes ? parent.width : root.timeColumnWidth
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

                        objectName: "logPaneLine"
                        x: line.stackTimes ? 0 : timeLabel.width + Theme.space.sectionGap
                        y: line.stackTimes ? timeLabel.height : 0
                        width: parent.width - x
                        color: Theme.colour.reviewText
                        font: Theme.font.rowText.font
                        lineHeightMode: Text.FixedHeight
                        lineHeight: root.rowPitch
                        textFormat: line.prefixText.length > 0 ? Text.StyledText : Text.PlainText
                        text: line.prefixText.length > 0 ? root.styledLine(line.prefixText, line.restText) : line.lineText
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                }
            }
        }
    }

    // What the log says when it has no entries yet.
    Column {
        objectName: "logPaneEmpty"
        visible: root.empty
        x: Theme.space.rowPadding
        y: list.y + Theme.space.rowPadding
        width: parent.width - 2 * x
        spacing: Theme.space.tileGap

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

    // ---- what floats over the foot of the list: a failure, and "Copied" ----

    Column {
        id: floating

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space.rowPadding
        anchors.rightMargin: Theme.space.rowPadding
        anchors.bottomMargin: root.toastBottom
        spacing: Theme.space.tileGap

        Banner {
            objectName: "logPaneNotice"
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

                objectName: "logPaneCopied"
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

    // ---- Share log ----

    PrimaryButton {
        id: shareButton

        objectName: "logPaneShare"
        visible: root.shareShown
        x: Theme.space.rowPadding
        y: parent.height - height - Theme.space.rowPadding
        width: parent.width - 2 * x
        enabled: root.share !== null && !root.share.sharing && !root.empty
        iconKind: "share"
        text: qsTr("Share log")
        onClicked: root.startShare()
    }

    Component.onCompleted: root.showNewest()
}
