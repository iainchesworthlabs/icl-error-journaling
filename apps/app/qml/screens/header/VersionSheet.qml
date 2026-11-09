// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The Software version sheet (Stage 4, package 4.9; design `ChangeVersion.dc.html`, HDR-07 and DAY-03): what happened to
// the version, in the facilitator's words.
//
//   "It changed during the day"   a Change: `session.changeHeader` records a timed entry in the log, at a time that
//                                 starts at now and may be earlier (the tumbler gives local seconds; the model turns
//                                 them into an instant, so QML does no time arithmetic)
//   "I typed it wrong"            a Correct: `session.correctHeader` fixes the value and writes no entry
//
// The sheet shows the exact line the log will get: it is `session.previewVersionChange`, made by the renderer from the
// same arguments `changeVersion` takes, and nothing here builds or edits it. The new value is checked as it is typed with
// `session.validateVersion`, and the button stays disabled until the value is valid and different.
//
// The commit of the build (HDR-09) is typed beside the version, in its own row and optional. The sheet takes the version and
// the commit as a pair, and neither is carried over from the version in force: the To row starts empty, so does the Commit
// row, and the version in force is shown with its commit in the From row ("1.4.2 (a1b2c3d)"). A change of the commit alone
// is a Change of the version, with the same number.
//
// Layout (ChangeVersion): the question in 15 pt, the two choices, then one card with From, To (the new value typed at the
// right of its row), Commit (the same) and Time (the time as a chip), then the line the log will get in a monospace face. The time is chosen
// on a second, shorter sheet (VersionTimeSheet) that the screen declares after this one: the Time row sets `timeOpen`.
//
// Open it with `show()`, which reads the version in force and the time now. The sheet closes itself when the change is
// accepted; Cancel, the grabber and Escape leave everything as it was. A refusal that only the model can know (an
// instant the clocks skip, or a value equal to the one in force at that instant) is shown under the card.
Sheet {
    id: root

    required property var session
    required property var timeline
    required property var settings

    // The two choices: true for "It changed during the day".
    property bool changing: true
    // The version in force when the sheet was opened, with its commit: "1.4.2 (a1b2c3d)".
    property string fromText: ""
    // The new version as typed, and its commit.
    property string toText: ""
    property string toCommit: ""
    // The time of the change in seconds since local midnight, and the time that "now" was when the sheet opened.
    property int timeSeconds: 0
    property int nowSeconds: 0
    property bool timeOpen: false
    // Why the model refused the last attempt to save, or empty.
    property string refusal: ""

    readonly property string trimmedTo: toText.trim()
    readonly property string toError: trimmedTo.length > 0 ? session.validateVersion(toText, "") : ""
    readonly property string commitError: toCommit.trim().length > 0 ? session.validateCommit(toCommit) : ""
    // The new version as the log prints it, or empty while either part is refused.
    readonly property string toDisplay: trimmedTo.length > 0 && toError.length === 0 && commitError.length === 0 ? session.versionDisplay(toText, toCommit) : ""
    readonly property bool unchanged: toDisplay.length > 0 && toDisplay === fromText
    readonly property bool canSave: toDisplay.length > 0 && !unchanged
    // The line the log will get, or the model's plain refusal of this change; empty while there is nothing to preview.
    readonly property string previewText: changing && toDisplay.length > 0 ? session.previewVersionChange(toText, toCommit, timeSeconds) : ""
    // The time as the log prints it. The format key is an argument only so that the label is made again when the format
    // changes.
    readonly property string timeLabel: formatTime(timeSeconds, settings.timeFormatKey)
    readonly property string timeValue: timeSeconds === nowSeconds ? qsTr("Now · %1").arg(timeLabel) : timeLabel

    function formatTime(seconds: int, formatKey: string): string {
        return formatKey.length >= 0 ? timeline.formatLocalSeconds(seconds) : "";
    }

    // Reads the version in force and the clock, and presents the sheet.
    function show(): void {
        fromText = session.versionDisplayText;
        toText = "";
        toCommit = "";
        changing = true;
        nowSeconds = session.nowLocalSeconds;
        timeSeconds = nowSeconds;
        timeOpen = false;
        refusal = "";
        open();
    }

    // Records the change or the correction. The model answers at once whether it accepted the command.
    function save(): void {
        const accepted = changing ? session.changeVersion(toText, toCommit, timeSeconds) : session.correctVersion(toText, toCommit);
        if (accepted)
            close();
        else
            refusal = changing && previewText.length > 0 ? previewText : qsTr("The version was not saved. Please try again.");
    }

    // The gaps of the design: 10 between the question and the cards and between the cards, 22 before each group.
    readonly property real gapSmall: Theme.space.tileGap + Theme.border.hairline * 2
    readonly property real gapGroup: Theme.space.screenMargin + Theme.space.tileGap - Theme.border.hairline * 2

    // The new value as a row of the card: the label at the left and the value typed at the right (not a FormField, which is
    // a field of its own with a heading and a message). Its field is `formInput`, as a FormField's is.
    component ToRow: Item {
        id: toRow

        required property string label
        property string text: ""
        property string placeholderText: ""
        property string errorText: ""
        property int inputMethodHints: Qt.ImhFormattedNumbersOnly
        readonly property bool hasFocus: input.activeFocus
        readonly property bool isLast: Positioner.isLastItem

        signal textEdited(string text)

        function focusField(): void {
            input.forceActiveFocus();
        }

        width: parent ? parent.width : implicitWidth
        implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
        implicitHeight: Math.max(Theme.size.rowHeightForm, toLabel.height + Theme.space.sectionGap, input.implicitHeight)
        onTextChanged: {
            if (input.text !== text)
                input.text = text;
        }
        Component.onCompleted: input.text = text

        TextMetrics {
            id: labelMetrics

            font: Theme.font.body.font
            text: toRow.label
        }

        // The field fills the row, so that a tap anywhere on it is a tap on the field (44 pt at least), and its text sits at
        // the right, after the label. A label that is wider than half the row wraps, at the largest text sizes.
        TextField {
            id: input

            objectName: "formInput"
            anchors.fill: parent
            leftPadding: toLabel.x + toLabel.width + Theme.space.tileGap
            rightPadding: Theme.space.rowPadding
            topPadding: Theme.space.tileGap
            bottomPadding: Theme.space.tileGap
            horizontalAlignment: TextInput.AlignRight
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.colour.textPrimary
            font: Theme.font.fieldValue.font
            placeholderText: toRow.placeholderText
            placeholderTextColor: Theme.colour.textPlaceholder
            selectionColor: Theme.colour.primaryTint
            selectedTextColor: Theme.colour.textPrimary
            inputMethodHints: toRow.inputMethodHints
            background: null

            Accessible.role: Accessible.EditableText
            Accessible.name: toRow.label
            Accessible.description: toRow.errorText

            onTextEdited: toRow.textEdited(text)
        }

        Text {
            id: toLabel

            x: Theme.space.rowPadding
            y: (parent.height - height) / 2
            width: Math.min(Math.ceil(labelMetrics.advanceWidth) + Theme.border.hairline, toRow.width / 2 - Theme.space.rowPadding)
            color: Theme.colour.textPrimary
            font: Theme.font.body.font
            text: toRow.label
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: Theme.border.hairline
            visible: !toRow.isLast
            color: Theme.colour.divider
        }
    }

    accessibleName: qsTr("Software version")
    detent: Sheet.Large
    onClosed: root.timeOpen = false

    Item {
        anchors.fill: parent

        NavBar {
            id: nav

            width: parent.width
            inSheet: true
            title: qsTr("Software version")
            leadingText: qsTr("Cancel")
            onLeadingTriggered: root.close()
        }

        Flickable {
            id: scroller

            // Brings an item of the column into view, for a field that the keyboard would otherwise cover.
            function reveal(item: Item): void {
                Keyboard.reveal(scroller, item, 0);
            }

            objectName: "versionScroller"
            y: nav.height
            width: parent.width
            height: Math.max(0, footer.y - y - Theme.space.sectionGap)
            clip: true
            contentWidth: width
            contentHeight: column.height + Theme.space.sectionGap
            boundsBehavior: Flickable.StopAtBounds
            // The keyboard shortens the sheet after the field has the focus: the field is brought into view again.
            onHeightChanged: Keyboard.revealFocused(scroller, 0)

            Column {
                id: column

                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                spacing: 0

                Text {
                    objectName: "versionQuestion"
                    width: parent.width
                    leftPadding: Theme.space.tileGap / 2
                    bottomPadding: root.gapSmall
                    color: Theme.colour.textSecondary
                    font: Theme.font.bodySmall.font
                    text: qsTr("What happened?")
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Accessible.name: text
                }

                Column {
                    width: parent.width
                    spacing: root.gapSmall

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
                }

                Item {
                    width: parent.width
                    height: root.gapGroup
                }

                // From, To and Time in one card, as the design has them.
                GroupedList {
                    objectName: "versionGroup"
                    width: parent.width
                    surface: Theme.colour.surfaceRaised

                    ListRow {
                        objectName: "versionFrom"
                        text: qsTr("From")
                        value: root.fromText
                        tappable: false
                    }

                    ToRow {
                        id: toField

                        objectName: "versionTo"
                        label: root.changing ? qsTr("To") : qsTr("Correct version")
                        text: root.toText
                        placeholderText: qsTr("For example 1.5.0")
                        errorText: root.toError.length > 0 ? root.toError : root.refusal
                        onTextEdited: text => {
                            root.toText = text;
                            root.refusal = "";
                        }
                        onHasFocusChanged: {
                            if (hasFocus)
                                scroller.reveal(toField);
                        }
                    }

                    // The commit of the build, optional (HDR-09): a row of its own under the version, in both choices.
                    ToRow {
                        id: commitRow

                        objectName: "versionCommit"
                        label: qsTr("Commit")
                        text: root.toCommit
                        placeholderText: qsTr("Optional")
                        errorText: root.commitError
                        inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase | Qt.ImhPreferLowercase
                        onTextEdited: text => {
                            root.toCommit = text;
                            root.refusal = "";
                        }
                        onHasFocusChanged: {
                            if (hasFocus)
                                scroller.reveal(commitRow);
                        }
                    }

                    ListRow {
                        objectName: "versionTime"
                        visible: root.changing
                        text: qsTr("Time")
                        value: root.timeValue
                        valueChip: true
                        accessibleName: qsTr("Time, %1. Opens the time picker.").arg(root.timeValue)
                        onActivated: root.timeOpen = true
                    }
                }

                // The refusal of the value, or of the change, under the card: a warning sign and the model's words.
                Item {
                    objectName: "versionRefusal"
                    readonly property string message: root.toError.length > 0 ? root.toError : root.commitError.length > 0 ? root.commitError : root.refusal

                    visible: message.length > 0
                    width: parent.width
                    height: visible ? refusalText.height + Theme.space.tileGap : 0

                    Icon {
                        x: Theme.space.tileGap / 2
                        y: Theme.space.tileGap + Math.max(0, (Theme.font.fieldMessage.linePitch - height) / 2)
                        size: Theme.size.badge * 2 / 3
                        kind: "warning"
                        colour: Theme.colour.destructive
                    }

                    Text {
                        id: refusalText

                        x: Theme.space.tileGap / 2 + Theme.size.badge * 2 / 3 + Theme.border.otherWidth * 4
                        y: Theme.space.tileGap
                        width: parent.width - x
                        color: Theme.colour.textPrimary
                        font: Theme.font.fieldMessage.font
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.font.fieldMessage.linePitch
                        text: parent.message
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }

                // What the commit is, under the card while nothing is refused (the hint of Today’s details, in the same words).
                Text {
                    objectName: "versionCommitHint"
                    visible: root.toError.length === 0 && root.commitError.length === 0 && root.refusal.length === 0
                    width: parent.width
                    height: visible ? implicitHeight : 0
                    topPadding: root.gapSmall
                    leftPadding: Theme.space.tileGap / 2
                    rightPadding: Theme.space.tileGap / 2
                    color: Theme.colour.textSecondary
                    font: Theme.font.footnote.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.footnote.linePitch
                    text: qsTr("The short code (7 to 12 letters and numbers) that identifies the exact build. Leave it empty if you can’t get it.")
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                Item {
                    visible: root.previewText.length > 0
                    width: parent.width
                    height: visible ? root.gapGroup : 0
                }

                Text {
                    visible: root.previewText.length > 0
                    width: parent.width
                    leftPadding: Theme.space.tileGap / 2
                    bottomPadding: Theme.space.tileGap
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
                        font: Theme.font.monospace.font
                        text: root.previewText
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                }

                Text {
                    objectName: "versionExplainer"
                    visible: root.previewText.length > 0 && !root.unchanged
                    width: parent.width
                    topPadding: root.gapSmall
                    leftPadding: Theme.space.tileGap / 2
                    rightPadding: Theme.space.tileGap / 2
                    color: Theme.colour.textSecondary
                    font: Theme.font.footnote.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.footnote.linePitch
                    text: qsTr("Entries before %1 keep %2. Entries after it use %3.").arg(root.timeLabel).arg(root.fromText).arg(root.toDisplay)
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }
            }
        }

        // The button ends at the bottom of the sheet's content area, 34 pt above the window's edge.
        Item {
            id: footer

            y: parent.height - height
            width: parent.width
            height: saveButton.height

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
