// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The time of a version change (design `TimeTumbler.dc.html`, decision 11 of docs/plan/stage-4.md): a second, shorter sheet
// over the Software version sheet, with Cancel, "Time" and Done, the hours and minutes (and seconds, and AM or PM, when the
// time format has them), a "Now" chip and the time as the log will show it. It is declared in the same SheetHost, after the
// version sheet, so it stacks over it with a scrim of its own.
//
//   version   the VersionSheet whose `timeSeconds` it edits. The sheet follows `version.timeOpen`: the Time row of the
//             version sheet sets it, and Cancel, Done and every way of dismissing the sheet clear it.
//
// The time moves the version sheet's line as the columns move, so the log line under the sheet shows the time that Done will
// keep. Cancel puts back the time the sheet was opened with. Dismissing the sheet with a swipe, a tap outside it or Escape
// is Done: what the columns show is kept.
Sheet {
    id: root

    required property var version
    required property var settings

    // The time the sheet was opened with, which Cancel puts back.
    property int savedSeconds: 0

    function sync(): void {
        if (root.version.timeOpen && !root.isOpen) {
            root.savedSeconds = root.version.timeSeconds;
            root.open();
        } else if (!root.version.timeOpen && root.isOpen) {
            root.close();
        }
    }

    accessibleName: qsTr("Time")
    detent: Sheet.Content
    contentImplicitHeight: nav.y + nav.implicitHeight + tumbler.implicitHeight
    onClosed: root.version.timeOpen = false
    Component.onCompleted: root.sync()

    Connections {
        function onTimeOpenChanged(): void {
            root.sync();
        }

        target: root.version
    }

    Item {
        anchors.fill: parent

        // The bar sits 8 pt up: its `inSheet` padding above the row is the 8 pt the sheet's grabber zone already has.
        NavBar {
            id: nav

            y: -Theme.space.tileGap
            width: parent.width
            inSheet: true
            title: qsTr("Time")
            leadingText: qsTr("Cancel")
            trailingText: qsTr("Done")
            onLeadingTriggered: {
                root.version.timeSeconds = root.savedSeconds;
                root.version.timeOpen = false;
            }
            onTrailingTriggered: root.version.timeOpen = false
        }

        TimeTumbler {
            id: tumbler

            objectName: "versionTumbler"
            x: Theme.space.screenMargin
            y: nav.y + nav.height
            width: parent.width - 2 * Theme.space.screenMargin
            localSeconds: root.version.timeSeconds
            twelveHour: root.settings.timeStyle === "12h"
            showSeconds: root.settings.timeSeconds
            nowText: qsTr("Now")
            caption: qsTr("Shown in the log as %1").arg(root.version.timeLabel)
            onTimeEdited: seconds => {
                root.version.timeSeconds = seconds;
                root.version.refusal = "";
            }
            onNowRequested: {
                root.version.timeSeconds = root.version.nowSeconds;
                root.version.refusal = "";
            }
        }
    }
}
