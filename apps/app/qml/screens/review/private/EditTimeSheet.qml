// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The time of an entry (design `TimeTumbler.dc.html` and `TimeTumbler12.dc.html`, decision 11 of docs/plan/stage-4.md): a
// second, shorter sheet over the Edit entry sheet, with Cancel, "Time" and Done, the hours and minutes (and seconds, and AM or
// PM, when the time format has them), a "Now" chip and the time as the log will show it. It is declared in the same SheetHost,
// after the Edit entry sheet, so it stacks over it with a scrim of its own.
//
//   edit   the EditEntry whose `localSeconds` it edits. The sheet follows `edit.timeOpen`: the Time row of Edit entry sets it,
//          and Cancel, Done and every way of dismissing the sheet clear it.
//
// The time moves the line of Edit entry as the columns move, so what is under the sheet shows the time that Done will keep.
// Cancel puts back the time the sheet was opened with. A swipe, a tap outside it or Escape is Done: what the columns show is
// kept, except that a time the model refuses (the hour the clocks skip) cannot be left that way; it is shown under the
// columns, and only Cancel leaves.
//
// "Now" is `timeline.nowLocalSeconds(entryId)`, the time of day now in the zone the entry was logged in.
//
// This file is a part of the Edit entry sheet and is tested through it (edit_entry_test.cpp, edit.time_sheet).
Sheet {
    id: root

    required property var edit
    required property var timeline
    required property var settings

    // The time the sheet was opened with, which Cancel puts back.
    property int savedSeconds: 0
    // What the content needs: the bar and the tumbler, the note of the repeated hour under it.
    readonly property real neededHeight: nav.implicitHeight + tumbler.implicitHeight + (note.visible ? note.implicitHeight : 0)
    // The design draws this sheet from 330 pt down to the bottom of the window, with room under the "Now" line, so the sheet
    // reaches that high on the design frame and never less than its content needs.
    readonly property real designTop: 330 // literal-ok: where the sheet of TimeTumbler.dc.html starts; the design has no token for it

    function sync(): void {
        if (root.edit.timeOpen && !root.isOpen) {
            root.savedSeconds = root.edit.localSeconds;
            root.open();
        } else if (!root.edit.timeOpen && root.isOpen) {
            root.close();
        }
    }

    // The time of day now, in the entry's zone; nothing happens when the zone is not known.
    function setToNow(): void {
        const now = root.timeline.nowLocalSeconds(root.edit.entryId);
        if (now >= 0)
            root.edit.localSeconds = now;
        root.edit.saveFailed = false;
    }

    objectName: "editTimeSheet"
    accessibleName: qsTr("Time")
    detent: Sheet.Content
    dismissible: root.edit.timeError.length === 0
    contentImplicitHeight: Math.max(neededHeight, root.height - designTop - grabberZone - bottomInset)
    onClosed: root.edit.timeOpen = false
    Component.onCompleted: root.sync()

    Connections {
        function onTimeOpenChanged(): void {
            root.sync();
        }

        target: root.edit
    }

    Item {
        anchors.fill: parent

        NavBar {
            id: nav

            width: parent.width
            inSheet: true
            title: qsTr("Time")
            leadingText: qsTr("Cancel")
            trailingText: qsTr("Done")
            // A time the model refuses cannot be kept.
            trailingEnabled: root.edit.timeError.length === 0
            onLeadingTriggered: {
                root.edit.localSeconds = root.savedSeconds;
                root.edit.timeOpen = false;
            }
            onTrailingTriggered: root.edit.timeOpen = false
        }

        TimeTumbler {
            id: tumbler

            objectName: "editTumbler"
            x: Theme.space.screenMargin
            y: nav.height
            width: parent.width - 2 * Theme.space.screenMargin
            localSeconds: root.edit.localSeconds
            twelveHour: root.settings.timeStyle === "12h"
            showSeconds: root.settings.timeSeconds
            errorText: root.edit.timeError
            nowText: qsTr("Now")
            caption: qsTr("Shown in the log as %1").arg(root.edit.shownTime)
            onTimeEdited: seconds => {
                root.edit.localSeconds = seconds;
                root.edit.saveFailed = false;
            }
            onNowRequested: root.setToNow()
        }

        // A time in the hour the clocks repeat is allowed, and says that the first one is used.
        Text {
            id: note

            objectName: "editTimeNote"
            visible: root.edit.timeNote.length > 0
            x: Theme.space.screenMargin + Theme.space.tileGap / 2
            y: tumbler.y + tumbler.height
            width: parent.width - 2 * x
            topPadding: Theme.space.tileGap
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: root.edit.timeNote
            wrapMode: Text.WordWrap
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }
    }
}
