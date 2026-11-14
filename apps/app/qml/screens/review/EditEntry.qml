// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The Edit entry sheet (Stage 4, package 4.10; REV-02, NOTE-06): Cancel and Save, the error type, the time, the note, the line
// as it will be in the shared log, and "Delete entry". The tester chose a separate sheet over editing in place.
//
//   timeline    the TimelineModel of the entry (entryForEdit, validateEdit, previewLine, saveEntry, deleteEntry, localToUtc)
//   picker      the ErrorPickerModel, the list of error types an error entry can be given
//   settings    the SettingsModel, for the time format of the time tumbler
//
// `edit(entryId)` opens the sheet on an entry; `editNote(entryId)` opens it with the cursor in the note, for the entry that a
// held tile has just logged (BRD-07, NOTE-06: Cancel then leaves the entry as it was). It shows the values `entryForEdit` gives and keeps the changes in a draft until
// Save; Cancel writes nothing. An error entry has a type, a time and an attached note; a note entry a time and its text; a
// header change a time and its new value. The kind of an entry cannot change.
//
// Layout (EditEntry): one card with the error type (its tile colour as a swatch before the name) and the time (as a chip, the
// way the design draws it), the note, the line as it will be in the shared log with the footnote that the model gives it
// ("Logged at 11:02 on UNIT-27, version v1.4.2."), and "Delete entry" at the bottom.
//
// The error type is chosen on a page of the same sheet that replaces the first one (Cancel and the title change with the
// page), so there is one set of controls in front of a screen reader at a time. The time is chosen on a second, shorter sheet
// (EditTimeSheet) that the screen declares after this one in the same SheetHost: the Time row sets `timeOpen`, and the time
// sheet is the one in front of a screen reader while it is up.
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
    // The list of people (the PeopleModel: `labelFor`), or null where there is none. The People row is then inert. An
    // interaction keeps the people it has even when they are not in the list (retired, or forgotten).
    property var people: null

    // The page in front: "main" or "type".
    property string page: "main"
    // The time sheet is up (EditTimeSheet follows it).
    property bool timeOpen: false
    // The person picker (declared after this sheet by the screen) is over it.
    property bool pickerOpen: false
    // The entry being edited, as `entryForEdit` gave it when the sheet opened.
    property string entryId
    property var original: ({})
    // The draft.
    property string chosenTypeId
    property string chosenTypeName
    // The tile colour slot of the chosen type (1 to 11, 0 when the type is gone), for the swatch of the Error row.
    property int chosenSlot: 0
    property int localSeconds: 0
    property string noteValue
    // An interaction: the people in the order picked (ids), the names the model gave for them, and the answer.
    property var chosenPeople: []
    property var personNames: ({})
    property string answerValue
    // The interaction types the entry can be changed to ({typeId, name}).
    property var interactionTypes: []
    // The last Save or Delete was refused by the model.
    property bool saveFailed: false
    // The sheet was opened for the note: the field takes the focus when the sheet has come up.
    property bool noteFirst: false

    readonly property string kind: original.kind ?? ""
    readonly property bool isHeaderChange: kind === "header_change"
    readonly property bool isInteraction: kind === "interaction"
    readonly property bool showsPeople: isInteraction && (original.asksPerson ?? false)
    readonly property bool showsAnswer: isInteraction && (original.asksText ?? false)
    // The people as the row says them; "(person not set)" when there are none.
    readonly property string peopleSummary: {
        if (chosenPeople.length === 0)
            return qsTr("(person not set)");
        const names = [];
        for (let i = 0; i < chosenPeople.length; ++i)
            names.push(nameOfPerson(chosenPeople[i]));
        return names.join(", ");
    }
    readonly property bool hasEntry: entryId.length > 0
    readonly property bool showingMain: page === "main"
    readonly property bool showingType: page === "type"
    // The gap between the groups of the sheet: 22 pt in the design.
    readonly property real groupGap: Theme.space.screenMargin + Theme.space.tileGap - Theme.border.hairline * 2

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
    // "Logged at 11:02 on UNIT-27, version v1.4.2.", from the model.
    readonly property string footnote: original.footnote ?? ""

    // The People row was tapped: the screen opens the person picker on the ordered ids, with the type's name and the location.
    signal peopleRequested(var picks, string typeName, string locationId)

    objectName: "editSheet"
    accessibleName: qsTr("Edit entry")
    dismissible: false
    // The time sheet has a scrim of its own, over this sheet and what is under it: this one lets go of its own, so that the
    // screen is dimmed once, as the design draws it.
    scrimColour: root.timeOpen || root.pickerOpen ? "transparent" : Theme.colour.sheetScrim
    onClosed: root.timeOpen = false
    onOpened: {
        if (root.noteFirst)
            noteInput.forceActiveFocus();
    }

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
        } else if (kind === "interaction") {
            edit.interactionTypeId = chosenTypeId;
            edit.personIds = chosenPeople;
            if (showsAnswer)
                edit.value = answerValue;
            edit.attachedNote = noteValue;
        }
        return edit;
    }

    // The name of a person for the rows: the list's, which says "someone" for a person who was forgotten; else the one the model
    // gave when the sheet opened.
    function nameOfPerson(id: string): string {
        if (people !== null)
            return people.labelFor(id);
        return personNames[id] ?? qsTr("someone");
    }

    // The people chosen in the picker (ordered ids, possibly none): they replace the picks of the draft.
    function setPeople(ids: var): void {
        chosenPeople = ids.slice();
        saveFailed = false;
    }

    // Opens the sheet on `id`. An entry that is not in the log opens nothing.
    function edit(id: string): void {
        openOn(id, false);
    }

    function openOn(id: string, focusNote: bool): void {
        const data = timeline.entryForEdit(id);
        if (data.entryId === undefined)
            return;
        noteFirst = focusNote;
        original = data;
        entryId = id;
        chosenTypeId = data.errorTypeId ?? "";
        chosenTypeName = data.errorTypeName ?? "";
        chosenSlot = data.colourSlot ?? 0;
        if (data.kind === "interaction") {
            chosenTypeId = data.interactionTypeId;
            chosenTypeName = data.interactionTypeName;
            chosenPeople = data.personIds ?? [];
            personNames = data.personNames ?? ({});
            answerValue = data.value ?? "";
            interactionTypes = timeline.interactionTypesFor(id);
            answerInput.text = answerValue;
        } else {
            chosenPeople = [];
            personNames = ({});
            answerValue = "";
            interactionTypes = [];
        }
        localSeconds = data.localSeconds;
        noteValue = data.attachedNote ?? data.noteText ?? data.newValue ?? "";
        saveFailed = false;
        noteInput.text = noteValue;
        page = "main";
        timeOpen = false;
        open();
    }

    // Opens the sheet on `id` with the cursor in the note.
    function editNote(id: string): void {
        openOn(id, true);
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

    function chooseType(id: string, name: string, slot: int): void {
        chosenTypeId = id;
        chosenTypeName = name;
        chosenSlot = slot;
        saveFailed = false;
        page = "main";
    }

    function showTimePage(): void {
        timeOpen = true;
    }

    // Cancel: on the first page it leaves, on the type page it goes back.
    function cancelPage(): void {
        if (page === "main")
            close();
        else
            page = "main";
    }

    // Save on the first page.
    function finishPage(): void {
        if (page === "main")
            save();
    }

    NavBar {
        id: nav

        width: parent.width
        // While the time sheet is over this one, nothing on this one takes a touch or is reached by a screen reader.
        enabled: !root.timeOpen && !root.pickerOpen
        inSheet: true
        title: root.showingType ? (root.isInteraction ? qsTr("Interaction") : qsTr("Error type")) : qsTr("Edit entry")
        leadingText: qsTr("Cancel")
        trailingText: root.showingMain ? qsTr("Save") : ""
        // A draft the model refuses cannot be saved.
        trailingEnabled: root.canSave
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
        enabled: !root.timeOpen && !root.pickerOpen

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
                spacing: root.groupGap
                bottomPadding: Theme.space.sectionGap

                GroupedList {
                    width: parent.width
                    surface: Theme.colour.surfaceRaised

                    ListRow {
                        objectName: "editType"
                        visible: root.kind === "error" || root.isInteraction
                        text: root.isInteraction ? qsTr("Interaction") : qsTr("Error")
                        value: root.chosenTypeName
                        valueSwatch: root.chosenSlot > 0 ? Theme.tileColour(root.chosenSlot) : "transparent"
                        minimumHeight: Theme.size.rowHeightForm
                        showChevron: true
                        onActivated: root.page = "type"
                    }

                    ListRow {
                        objectName: "editPeople"
                        visible: root.showsPeople
                        text: qsTr("People")
                        value: root.peopleSummary
                        minimumHeight: Theme.size.rowHeightForm
                        showChevron: root.people !== null
                        enabled: root.people !== null
                        onActivated: root.peopleRequested(root.chosenPeople, root.chosenTypeName, root.original.locationId ?? "")
                    }

                    ListRow {
                        objectName: "editTime"
                        text: qsTr("Time")
                        value: root.shownTime
                        valueChip: true
                        minimumHeight: Theme.size.rowHeightForm
                        onActivated: root.showTimePage()
                    }
                }

                // The answer of an interaction that has one.
                Column {
                    width: parent.width
                    visible: root.showsAnswer

                    Text {
                        x: Theme.space.tileGap / 2
                        width: parent.width - 2 * x
                        height: implicitHeight + Theme.space.tileGap
                        color: Theme.colour.textSecondary
                        font: Theme.font.sectionLabel.font
                        text: qsTr("Answer").toLocaleUpperCase()
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    Rectangle {
                        width: parent.width
                        height: Math.max(Theme.size.rowHeightForm, answerInput.implicitHeight)
                        radius: Theme.radius.groupedList
                        color: Theme.colour.surfaceRaised
                        border.width: answerInput.activeFocus ? Theme.border.hairline * 2 : 0
                        border.color: Theme.colour.primaryText

                        TextArea {
                            id: answerInput

                            objectName: "editAnswer"
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
                            Accessible.name: qsTr("Answer")

                            onTextChanged: {
                                if (text !== root.answerValue) {
                                    root.answerValue = text;
                                    root.saveFailed = false;
                                }
                            }
                        }
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
                        // 104 pt, as drawn; it grows with a longer note.
                        height: Math.max(Theme.size.tileHeight, noteInput.implicitHeight)
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
                            font: Theme.font.previewText.font
                            lineHeightMode: Text.FixedHeight
                            lineHeight: Theme.font.previewText.linePitch
                            text: root.preview.length > 0 ? root.preview : qsTr("The line cannot be shown until the entry is valid.")
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }

                    // Where and when it was logged, under the line: 10 pt below it and 4 pt in.
                    Text {
                        objectName: "editFootnote"
                        x: Theme.space.tileGap / 2
                        width: parent.width - 2 * x
                        topPadding: Theme.space.tileGap + Theme.border.hairline * 2
                        visible: root.footnote.length > 0
                        height: visible ? implicitHeight : 0
                        color: Theme.colour.textSecondary
                        font: Theme.font.footnote.font
                        lineHeightMode: Text.FixedHeight
                        lineHeight: Theme.font.footnote.linePitch
                        text: root.footnote
                        wrapMode: Text.WordWrap
                        Accessible.role: Accessible.StaticText
                        Accessible.name: text
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

        // Delete entry, in its own button below the fields: it is destructive and the swipe is only a shortcut to it. It sits
        // at the bottom of the sheet, on the home indicator, as the design has it.
        Item {
            id: deleteButton

            objectName: "editDelete"
            x: Theme.space.screenMargin
            y: parent.height - height
            width: parent.width - 2 * Theme.space.screenMargin
            height: Math.max(Theme.size.rowHeightForm, deleteLabel.implicitHeight + 2 * Theme.space.sectionGap)
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

    // ---- the error type ----

    Flickable {
        id: typePage

        y: nav.height
        width: parent.width
        height: Math.max(0, parent.height - y)
        visible: root.showingType
        enabled: !root.timeOpen && !root.pickerOpen
        contentWidth: width
        contentHeight: typeColumn.height
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: typeColumn

            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            bottomPadding: Theme.space.sectionGap

            // The interaction types: the ones that are not retired, and the entry's own.
            GroupedList {
                width: parent.width
                visible: root.isInteraction
                surface: Theme.colour.surfaceRaised

                Repeater {
                    model: root.isInteraction ? root.interactionTypes : []

                    delegate: ListRow {
                        required property var modelData

                        objectName: "interactionTypeRow"
                        text: modelData.name
                        checkable: true
                        checked: modelData.typeId === root.chosenTypeId
                        onActivated: root.chooseType(modelData.typeId, modelData.name, 0)
                    }
                }
            }

            GroupedList {
                width: parent.width
                visible: !root.isInteraction
                surface: Theme.colour.surfaceRaised

                Repeater {
                    model: root.isInteraction ? null : root.picker

                    delegate: ListRow {
                        required property string typeId
                        required property string label
                        required property int colourSlot

                        objectName: "typeRow"
                        text: label
                        checkable: true
                        checked: typeId === root.chosenTypeId
                        onActivated: root.chooseType(typeId, label, colourSlot)
                    }
                }
            }
        }
    }
}
