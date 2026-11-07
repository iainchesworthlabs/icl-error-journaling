// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The Software version sheet (Stage 4, package 4.9; design `ChangeVersion.dc.html`, HDR-07 and DAY-03): what happened to
// the version, in the facilitator's words.
//
//   "It changed during the day"   a Change: `session.changeHeader` records a timed entry in the log, at a time that
//                                 starts at now and may be earlier (the tumbler gives local seconds; the model turns
//                                 them into an instant, so QML does no time arithmetic)
//   "I typed it wrong"            a Correct: `session.correctHeader` fixes the value and writes no entry
//
// The sheet shows the exact line the log will get: it is `session.previewHeaderChange`, made by the renderer from the
// same arguments `changeHeader` takes, and nothing here builds or edits it. The new value is checked as it is typed with
// `session.validateHeaderValue`, and the button stays disabled until the value is valid and different.
//
// Open it with `show()`, which reads the version in force and the time now. The sheet closes itself when the change is
// accepted; Cancel, the grabber and Escape leave everything as it was. A refusal that only the model can know (an
// instant the clocks skip, or a value equal to the one in force at that instant) is shown under the new value.
Sheet {
    id: root

    required property var session
    required property var timeline
    required property var settings

    readonly property string fieldKey: "software_version"

    // The two choices: true for "It changed during the day".
    property bool changing: true
    // The version in force when the sheet was opened.
    property string fromText: ""
    // The new version as typed.
    property string toText: ""
    // The time of the change in seconds since local midnight, and the time that "now" was when the sheet opened.
    property int timeSeconds: 0
    property int nowSeconds: 0
    property bool timeOpen: false
    // Why the model refused the last attempt to save, or empty.
    property string refusal: ""

    readonly property string trimmedTo: toText.trim()
    readonly property string toError: trimmedTo.length > 0 ? session.validateHeaderValue(fieldKey, toText) : ""
    readonly property bool unchanged: trimmedTo === fromText
    readonly property bool canSave: trimmedTo.length > 0 && toError.length === 0 && !unchanged
    // The line the log will get, or the model's plain refusal of this change; empty while there is nothing to preview.
    readonly property string previewText: changing && trimmedTo.length > 0 && toError.length === 0 ? session.previewHeaderChange(fieldKey, toText, timeSeconds) : ""
    // The time as the log prints it. The format key is an argument only so that the label is made again when the format
    // changes.
    readonly property string timeLabel: formatTime(timeSeconds, settings.timeFormatKey)
    readonly property string timeValue: timeSeconds === nowSeconds ? qsTr("Now · %1").arg(timeLabel) : timeLabel

    function formatTime(seconds: int, formatKey: string): string {
        return formatKey.length >= 0 ? timeline.formatLocalSeconds(seconds) : "";
    }

    // Reads the version in force and the clock, and presents the sheet.
    function show(): void {
        fromText = session.versionText;
        toText = "";
        changing = true;
        nowSeconds = session.nowLocalSeconds;
        timeSeconds = nowSeconds;
        timeOpen = false;
        refusal = "";
        open();
    }

    // Records the change or the correction. The model answers at once whether it accepted the command.
    function save(): void {
        const accepted = changing ? session.changeHeader(fieldKey, toText, timeSeconds) : session.correctHeader(fieldKey, toText);
        if (accepted)
            close();
        else
            refusal = changing && previewText.length > 0 ? previewText : qsTr("The version was not saved. Please try again.");
    }

    accessibleName: qsTr("Software version")
    detent: Sheet.Large

    Item {
        anchors.fill: parent

        NavBar {
            id: nav

            width: parent.width
            title: qsTr("Software version")
            leadingText: qsTr("Cancel")
            onLeadingTriggered: root.close()
        }

        Flickable {
            id: scroller

            // Brings an item of the column into view, for a field that the keyboard would otherwise cover.
            function reveal(item: Item): void {
                const top = item.mapToItem(column, 0, 0).y;
                const bottom = top + item.height + Theme.space.sectionGap;
                if (top < contentY)
                    contentY = Math.max(0, top - Theme.space.sectionGap);
                else if (bottom > contentY + height)
                    contentY = Math.min(Math.max(0, contentHeight - height), bottom - height);
            }

            objectName: "versionScroller"
            y: nav.height
            width: parent.width
            height: Math.max(0, footer.y - nav.height)
            clip: true
            contentWidth: width
            contentHeight: column.height + Theme.space.sectionGap
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: column

                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                spacing: Theme.space.sectionGap

                Text {
                    width: parent.width
                    leftPadding: Theme.space.tileGap / 2
                    color: Theme.colour.textSecondary
                    font: Theme.font.sectionLabel.font
                    text: qsTr("What happened?").toLocaleUpperCase()
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: qsTr("What happened?")
                }

                ChoiceCard {
                    objectName: "versionChanged"
                    title: qsTr("It changed during the day")
                    description: qsTr("For example an over-the-air update. Adds a timed entry to the log.")
                    selected: root.changing
                    onClicked: root.changing = true
                }

                ChoiceCard {
                    objectName: "versionTypo"
                    title: qsTr("I typed it wrong")
                    description: qsTr("Corrects the value. Nothing is added to the log.")
                    selected: !root.changing
                    onClicked: root.changing = false
                }

                GroupedList {
                    width: parent.width
                    surface: Theme.colour.surfaceRaised

                    ListRow {
                        objectName: "versionFrom"
                        text: qsTr("From")
                        value: root.fromText
                        tappable: false
                    }
                }

                FormField {
                    id: toField

                    objectName: "versionTo"
                    label: root.changing ? qsTr("To") : qsTr("Correct version")
                    text: root.toText
                    placeholderText: qsTr("For example 1.5.0")
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    errorText: root.toError.length > 0 ? root.toError : root.refusal
                    valid: root.canSave
                    onTextEdited: text => {
                        root.toText = text;
                        root.refusal = "";
                    }
                    onHasFocusChanged: {
                        if (hasFocus)
                            scroller.reveal(toField);
                    }
                }

                GroupedList {
                    visible: root.changing
                    width: parent.width
                    surface: Theme.colour.surfaceRaised

                    ListRow {
                        objectName: "versionTime"
                        text: qsTr("Time")
                        value: root.timeValue
                        accessibleName: qsTr("Time, %1. Opens the time picker.").arg(root.timeValue)
                        onActivated: root.timeOpen = !root.timeOpen
                    }
                }

                TimeTumbler {
                    objectName: "versionTumbler"
                    visible: root.changing && root.timeOpen
                    height: visible ? implicitHeight : 0
                    localSeconds: root.timeSeconds
                    twelveHour: root.settings.timeStyle === "12h"
                    showSeconds: root.settings.timeSeconds
                    onTimeEdited: seconds => {
                        root.timeSeconds = seconds;
                        root.refusal = "";
                    }
                }

                Text {
                    visible: root.previewText.length > 0
                    width: parent.width
                    leftPadding: Theme.space.tileGap / 2
                    color: Theme.colour.textSecondary
                    font: Theme.font.sectionLabel.font
                    text: qsTr("Adds to your log").toLocaleUpperCase()
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: qsTr("Adds to your log")
                }

                Rectangle {
                    visible: root.previewText.length > 0
                    width: parent.width
                    height: previewLine.height + 2 * Theme.space.rowPadding
                    radius: Theme.radius.groupedList
                    color: Theme.colour.surfaceRaised
                    border.width: root.unchanged || root.refusal.length > 0 ? Theme.border.hairline * 2 : 0
                    border.color: Theme.colour.destructive
                    Accessible.role: Accessible.StaticText
                    Accessible.name: root.previewText

                    Text {
                        id: previewLine

                        objectName: "versionPreview"
                        x: Theme.space.rowPadding
                        y: Theme.space.rowPadding
                        width: parent.width - 2 * Theme.space.rowPadding
                        color: root.unchanged || root.refusal.length > 0 ? Theme.colour.destructive : Theme.colour.textPrimary
                        font: Theme.font.timeColumn.font
                        text: root.previewText
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                }

                Text {
                    objectName: "versionExplainer"
                    visible: root.previewText.length > 0 && !root.unchanged
                    width: parent.width
                    leftPadding: Theme.space.tileGap / 2
                    rightPadding: Theme.space.tileGap / 2
                    color: Theme.colour.textSecondary
                    font: Theme.font.caption.font
                    text: qsTr("Entries before %1 keep %2. Entries after it use %3.").arg(root.timeLabel).arg(root.fromText).arg(root.trimmedTo)
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }

        Item {
            id: footer

            y: parent.height - height
            width: parent.width
            height: saveButton.height + Theme.space.sectionGap

            PrimaryButton {
                id: saveButton

                objectName: "versionSave"
                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                text: root.changing ? qsTr("Log the change") : qsTr("Correct the version")
                enabled: root.canSave
                onClicked: root.save()
            }
        }
    }
}
