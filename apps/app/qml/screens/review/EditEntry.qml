// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The Edit entry sheet (Stage 4, package 4.10; REV-02, NOTE-06): Cancel and Save, the error type, the time, the note, the line
// as it will be in the shared log, and "Delete entry". The facilitator chose a separate sheet over editing in place.
//
//   timeline    the TimelineModel of the entry (entryForEdit, validateEdit, previewLine, saveEntry, deleteEntry, localToUtc)
//   picker      the ErrorPickerModel, the list of error types an error entry can be given
//   settings    the SettingsModel, for the time format of the time tumbler
//
// `edit(entryId)` opens the sheet on an entry. It shows the values `entryForEdit` gives and keeps the changes in a draft until
// Save; Cancel writes nothing. An error entry has a type, a time and an attached note; a note entry a time and its text; a
// header change a time and its new value. The kind of an entry cannot change.
//
// The time and the error type are chosen on pages of the same sheet that replace the first one (Cancel, the title and Done
// or Save change with the page), so there is one set of controls in front of a screen reader at a time.
//
// Every rule is the model's. The sheet sends `validateEdit` the edit it has and shows the refusal; it shows `previewLine`,
// which the renderer makes on a scratch copy and which is the line `saveEntry` then saves. Before a time is saved it goes
// through `localToUtc`: that is the only thing that refuses a time in the hour the clocks skip, and `saveEntry` alone would
// move such a time without a word. The refusal is shown under the tumbler, and while it stands neither Done nor Save is
// possible, so `saveEntry` is never called with a refused time.
//
// Delete is `deleteEntry` and nothing else: the toast that offers Undo is the UndoController's, in the shell, and there is no
// dialog (REV-03). Saving and deleting fire no haptic. Cancel and Save are the only ways out: a swipe or a tap outside must
// not throw a change away.
Sheet {
    id: root

    required property var timeline
    required property var picker
    required property var settings

    // The page in front: "main", "time" or "type".
    property string page: "main"
    // The entry being edited, as `entryForEdit` gave it when the sheet opened.
    property string entryId
    property var original: ({})
    // The draft.
    property string chosenTypeId
    property string chosenTypeName
    property int localSeconds: 0
    property string noteValue
    // The time the entry had when the time page opened, for Cancel.
    property int timeBefore: 0
    // The last Save or Delete was refused by the model.
    property bool saveFailed: false

    readonly property string kind: original.kind ?? ""
    readonly property bool isHeaderChange: kind === "header_change"
    readonly property bool hasEntry: entryId.length > 0
    readonly property bool showingMain: page === "main"
    readonly property bool showingTime: page === "time"
    readonly property bool showingType: page === "type"

    // The time, through the model: `ok`, `message` and `repeated`.
    readonly property var timeCheck: hasEntry ? timeline.localToUtc(entryId, original.localDate, localSeconds) : ({
            "ok": true,
            "message": "",
            "repeated": false
        })
    // Why the time is refused (the hour the clocks skip); empty when it is not.
    readonly property string timeError: timeCheck.ok ? "" : timeCheck.message
    // A time in the hour the clocks repeat is allowed, with a note that the first is used.
    readonly property string timeNote: timeCheck.ok && timeCheck.repeated ? timeCheck.message : ""
    readonly property string shownTime: hasEntry ? timeline.formatLocalSeconds(localSeconds) : ""
    // What the draft would save, and the model's verdict on it.
    readonly property var edited: buildEdit()
    readonly property string refusal: hasEntry ? timeline.validateEdit(entryId, edited) : ""
    readonly property bool canSave: hasEntry && timeError.length === 0 && refusal.length === 0
    readonly property string preview: canSave ? timeline.previewLine(entryId, edited) : ""

    objectName: "editSheet"
    accessibleName: qsTr("Edit entry")
    dismissible: false

    // The map `validateEdit`, `previewLine` and `saveEntry` take. A key left out keeps the entry's value, so each kind sends
    // the keys it can change.
    function buildEdit(): var {
        const edit = {
            "localDate": original.localDate,
            "localSeconds": localSeconds
        };
        if (kind === "error") {
            edit.errorTypeId = chosenTypeId;
            edit.attachedNote = noteValue;
        } else if (kind === "note") {
            edit.noteText = noteValue;
        } else if (kind === "header_change") {
            edit.newValue = noteValue;
        }
        return edit;
    }

    // Opens the sheet on `id`. An entry that is not in the log opens nothing.
    function edit(id: string): void {
        const data = timeline.entryForEdit(id);
        if (data.entryId === undefined)
            return;
        original = data;
        entryId = id;
        chosenTypeId = data.errorTypeId ?? "";
        chosenTypeName = data.errorTypeName ?? "";
        localSeconds = data.localSeconds;
        noteValue = data.attachedNote ?? data.noteText ?? data.newValue ?? "";
        saveFailed = false;
        noteInput.text = noteValue;
        page = "main";
        open();
    }

    function save(): void {
        if (!canSave)
            return;
        if (timeline.saveEntry(entryId, edited))
            close();
        else
            saveFailed = true;
    }

    function deleteEntry(): void {
        if (!hasEntry)
            return;
        if (timeline.deleteEntry(entryId))
            close();
        else
            saveFailed = true;
    }

    function chooseType(id: string, name: string): void {
        chosenTypeId = id;
        chosenTypeName = name;
        saveFailed = false;
        page = "main";
    }

    function showTimePage(): void {
        timeBefore = localSeconds;
        page = "time";
    }

    // Cancel: on the first page it leaves, on the others it goes back and puts back what the page changed.
    function cancelPage(): void {
        if (page === "time")
            localSeconds = timeBefore;
        if (page === "main")
            close();
        else
            page = "main";
    }

    // Save on the first page, Done on the time page.
    function finishPage(): void {
        if (page === "time") {
            if (timeError.length === 0)
                page = "main";
        } else if (page === "main") {
            save();
        }
    }

    NavBar {
        id: nav

        width: parent.width
        title: root.showingTime ? qsTr("Time") : root.showingType ? qsTr("Error type") : qsTr("Edit entry")
        leadingText: qsTr("Cancel")
        trailingText: root.showingTime ? qsTr("Done") : root.showingType ? "" : qsTr("Save")
        // A time the model refuses cannot be kept, and a draft it refuses cannot be saved.
        trailingEnabled: root.showingTime ? root.timeError.length === 0 : root.canSave
        onLeadingTriggered: root.cancelPage()
        onTrailingTriggered: root.finishPage()
    }

    // ---- the first page ----

    Item {
        id: mainPage

        y: nav.height
        width: parent.width
        height: parent.height - y
        visible: root.showingMain

        Flickable {
            id: flick

            width: parent.width
            height: Math.max(0, deleteButton.y - Theme.space.sectionGap)
            contentWidth: width
            contentHeight: column.height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: column

                x: Theme.space.screenMargin
                width: flick.width - 2 * Theme.space.screenMargin
                spacing: Theme.space.screenMargin + Theme.space.tileGap / 2
                topPadding: Theme.space.screenMargin
                bottomPadding: Theme.space.sectionGap

                GroupedList {
                    width: parent.width
                    surface: Theme.colour.surfaceRaised

                    ListRow {
                        objectName: "editType"
                        visible: root.kind === "error"
                        text: qsTr("Error")
                        value: root.chosenTypeName
                        showChevron: true
                        onActivated: root.page = "type"
                    }

                    ListRow {
                        objectName: "editTime"
                        text: qsTr("Time")
                        value: root.shownTime
                        showChevron: true
                        onActivated: root.showTimePage()
                    }
                }

                // The note, or the new value of a header change.
                Column {
                    width: parent.width

                    Text {
                        x: Theme.space.tileGap / 2
                        width: parent.width - 2 * x
                        height: implicitHeight + Theme.space.tileGap
                        color: Theme.colour.textSecondary
                        font: Theme.font.sectionLabel.font
                        text: (root.isHeaderChange ? qsTr("New value") : qsTr("Note")).toLocaleUpperCase()
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    Rectangle {
                        id: noteBox

                        objectName: "editNoteBox"
                        width: parent.width
                        height: Math.max(Theme.size.fieldHeight * 2 + Theme.space.sectionGap, noteInput.implicitHeight)
                        radius: Theme.radius.groupedList
                        color: Theme.colour.surfaceRaised
                        border.width: noteInput.activeFocus ? Theme.border.hairline * 2 : 0
                        border.color: Theme.colour.primaryText

                        TextArea {
                            id: noteInput

                            objectName: "editNote"
                            width: parent.width
                            height: parent.height
                            leftPadding: Theme.space.rowPadding
                            rightPadding: Theme.space.rowPadding
                            topPadding: Theme.space.sectionGap
                            bottomPadding: Theme.space.sectionGap
                            color: Theme.colour.textPrimary
                            font: Theme.font.body.font
                            wrapMode: TextEdit.Wrap
                            selectionColor: Theme.colour.primaryTint
                            selectedTextColor: Theme.colour.textPrimary
                            background: null

                            Accessible.role: Accessible.EditableText
                            Accessible.name: root.isHeaderChange ? qsTr("New value") : qsTr("Note for this entry")
                            Accessible.description: root.refusal

                            onTextChanged: {
                                if (text !== root.noteValue) {
                                    root.noteValue = text;
                                    root.saveFailed = false;
                                }
                            }
                        }
                    }

                    Text {
                        objectName: "editRefusal"
                        x: Theme.space.tileGap / 2
                        width: parent.width - 2 * x
                        height: visible ? implicitHeight + Theme.space.tileGap : 0
                        visible: root.refusal.length > 0
                        verticalAlignment: Text.AlignBottom
                        color: Theme.colour.destructive
                        font: Theme.font.caption.font
                        text: root.refusal
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }
                }

                // The line as the shared log will have it.
                Column {
                    width: parent.width

                    Text {
                        x: Theme.space.tileGap / 2
                        width: parent.width - 2 * x
                        height: implicitHeight + Theme.space.tileGap
                        color: Theme.colour.textSecondary
                        font: Theme.font.sectionLabel.font
                        text: qsTr("In the shared log").toLocaleUpperCase()
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    Rectangle {
                        width: parent.width
                        height: previewText.implicitHeight + 2 * Theme.space.rowPadding
                        radius: Theme.radius.groupedList
                        color: Theme.colour.surfaceRaised
                        Accessible.role: Accessible.StaticText
                        Accessible.name: qsTr("In the shared log: %1").arg(previewText.text)

                        Text {
                            id: previewText

                            objectName: "editPreview"
                            x: Theme.space.rowPadding
                            y: Theme.space.rowPadding
                            width: parent.width - 2 * Theme.space.rowPadding
                            color: root.preview.length > 0 ? Theme.colour.textPrimary : Theme.colour.textSecondary
                            font: Theme.font.bodySmall.font
                            text: root.preview.length > 0 ? root.preview : qsTr("The line cannot be shown until the entry is valid.")
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    Text {
                        objectName: "editSaveFailed"
                        x: Theme.space.tileGap / 2
                        width: parent.width - 2 * x
                        height: visible ? implicitHeight + Theme.space.tileGap : 0
                        visible: root.saveFailed
                        verticalAlignment: Text.AlignBottom
                        color: Theme.colour.destructive
                        font: Theme.font.caption.font
                        text: qsTr("The change could not be saved. Try again.")
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
                    }
                }
            }
        }

        // Delete entry, in its own button below the fields: it is destructive and the swipe is only a shortcut to it.
        Item {
            id: deleteButton

            objectName: "editDelete"
            x: Theme.space.screenMargin
            y: parent.height - height - Theme.space.sectionGap
            width: parent.width - 2 * Theme.space.screenMargin
            height: Math.max(Theme.size.primaryButtonHeight, deleteLabel.implicitHeight + 2 * Theme.space.sectionGap)
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Delete entry")
            Accessible.focusable: true
            Accessible.onPressAction: root.deleteEntry()

            Rectangle {
                anchors.fill: parent
                radius: Theme.radius.primaryButton
                color: deleteTap.pressed ? Theme.colour.primaryTint : Theme.colour.surfaceRaised
                border.width: deleteButton.activeFocus ? Theme.border.otherWidth : 0
                border.color: Theme.colour.primaryText
            }

            Text {
                id: deleteLabel

                anchors.fill: parent
                anchors.leftMargin: Theme.space.rowPadding
                anchors.rightMargin: Theme.space.rowPadding
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: Theme.colour.destructive
                font: Theme.font.sheetTitle.font
                text: qsTr("Delete entry")
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }

            TapHandler {
                id: deleteTap

                onTapped: root.deleteEntry()
            }
            Keys.onReturnPressed: root.deleteEntry()
            Keys.onSpacePressed: root.deleteEntry()
        }
    }

    // ---- the time ----

    Flickable {
        id: timePage

        y: nav.height
        width: parent.width
        height: Math.max(0, parent.height - y)
        visible: root.showingTime
        contentWidth: width
        contentHeight: timeColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: timeColumn

            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.sectionGap
            topPadding: Theme.space.screenMargin
            bottomPadding: Theme.space.sectionGap

            TimeTumbler {
                objectName: "editTumbler"
                width: parent.width
                localSeconds: root.localSeconds
                twelveHour: root.settings.timeStyle === "12h"
                showSeconds: root.settings.timeSeconds
                errorText: root.timeError
                onTimeEdited: seconds => {
                    root.localSeconds = seconds;
                    root.saveFailed = false;
                }
            }

            Text {
                objectName: "editShownTime"
                width: parent.width
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("Shown in the log as %1").arg(root.shownTime)
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Text {
                objectName: "editTimeNote"
                visible: root.timeNote.length > 0
                width: parent.width
                color: Theme.colour.textSecondary
                font: Theme.font.caption.font
                text: root.timeNote
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    // ---- the error type ----

    Flickable {
        id: typePage

        y: nav.height
        width: parent.width
        height: Math.max(0, parent.height - y)
        visible: root.showingType
        contentWidth: width
        contentHeight: typeColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: typeColumn

            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            topPadding: Theme.space.screenMargin
            bottomPadding: Theme.space.sectionGap

            GroupedList {
                width: parent.width
                surface: Theme.colour.surfaceRaised

                Repeater {
                    model: root.picker

                    delegate: ListRow {
                        required property string typeId
                        required property string label

                        objectName: "typeRow"
                        text: label
                        checkable: true
                        checked: typeId === root.chosenTypeId
                        onActivated: root.chooseType(typeId, label)
                    }
                }
            }
        }
    }
}
