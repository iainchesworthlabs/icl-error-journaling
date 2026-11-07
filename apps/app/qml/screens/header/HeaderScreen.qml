// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// Today's details (Stage 4, package 4.9; design `Header.dc.html`, `HeaderNewLogRow.dc.html` and
// `StartNewLogConfirm.dc.html`; HDR-01 to HDR-07 and DAY-02): the facilitator and the date, read only, then the three
// fields of the day's header, then the way to start a new log, and "Start logging" at the foot.
//
//   unit number       a fixed prefix the person does not type, and the number pad. The number is checked as it is typed
//                     with `session.validateHeaderValue`, and a valid one is saved with `session.correctHeader`
//   location          the configured locations as a list of choices, and Other with a text field
//   software version  a field checked the same way. A first fill, and a typo, are a Correct (HDR-07); a version that
//                     changed during the day is a Change, which the Software version sheet records
//
// Nothing here blocks logging (HDR-06): "Start logging" only closes the screen, and an empty or refused field is left as
// it was. The screen holds what is typed, and the model holds what is saved; a value the model refuses is never sent.
//
// The last row, "Start a new log for another unit or site" (decision 8), asks first, because it archives the open log: the
// alert says how many entries the log has (`session.liveEntryCount`) and then `session.startNewLog()` runs.
//
// The screen is a SheetHost, so the Software version sheet and the alert cover it, and it needs a window. The shell
// hands it the models and listens to `closeRequested`.
SheetHost {
    id: root

    required property var session
    required property var settings
    // For the time of the version sheet (`formatLocalSeconds`); the screen does not use it itself.
    required property var timeline

    // "Log" or "Start logging" was pressed: the shell leaves this screen.
    signal closeRequested

    // The safe-area insets, which a test overrides.
    property bool overrideInsets: false
    property real overrideTop: 0
    property real overrideBottom: 0

    // The height of the keyboard in points, 0 when there is none (assumption U5). A test sets it directly.
    property real keyboardHeight: inputMethod.visible ? inputMethod.keyboardRectangle.height : 0
    // The platform's input method, untyped because the QML type system sees it only as a QObject.
    readonly property var inputMethod: Qt.inputMethod

    // What is typed, what the model was last given, and whether the person has typed in the field yet. A field shows no
    // refusal until it has been edited, so an empty field is not scolded when the screen opens.
    property string unitDraft: ""
    property string unitSaved: ""
    property bool unitTouched: false
    property bool otherOpen: false
    property string otherDraft: ""
    property string otherSaved: ""
    property bool otherTouched: false
    property string versionDraft: ""
    property string versionSaved: ""
    property bool versionTouched: false

    readonly property string unitLead: qsTr("%1-").arg(settings.unitPrefix)
    readonly property string unitError: unitTouched ? session.validateHeaderValue("unit_number", unitDraft) : ""
    readonly property string otherError: otherTouched ? session.validateHeaderValue("location", otherDraft) : ""
    readonly property string versionError: versionTouched ? session.validateHeaderValue("software_version", versionDraft) : ""
    readonly property string entriesText: session.liveEntryCount === 1 ? qsTr("1 entry") : qsTr("%1 entries").arg(session.liveEntryCount)
    // Which log the alert is about: "UNIT-27 at North Site", or what is known of it.
    readonly property string logLabel: {
        const unit = session.unitText;
        const place = session.locationText;
        if (unit.length > 0 && place.length > 0)
            return qsTr("%1 at %2").arg(unit).arg(place);
        if (unit.length > 0 || place.length > 0)
            return unit.length > 0 ? unit : place;
        return qsTr("Today's log");
    }
    readonly property string newLogMessage: qsTr("%1 has %2. It stays in History. The new log starts empty, with %3 as facilitator.").arg(logLabel).arg(entriesText).arg(session.facilitator)
    // The field being typed in, kept in view above the keyboard.
    property Item activeField: null

    // The digits of the unit number in force: the model gives "UNIT-27" and the screen shows the prefix apart.
    function unitDigits(): string {
        const text = session.unitText;
        return text.startsWith(unitLead) ? text.slice(unitLead.length) : text;
    }

    // Saves `text` as the value of a field when the model accepts it and it is not what was saved already.
    function correct(fieldKey: string, text: string, saved: string): bool {
        const value = text.trim();
        if (value === saved || session.validateHeaderValue(fieldKey, value).length > 0)
            return false;
        return session.correctHeader(fieldKey, value);
    }

    // A field is saved when the person is done with it: Return, leaving the field, "Start logging", going back, and the
    // app going to the background. Saving as they type would write "1.4.2" on the way to "1.4.20". A value the model
    // refuses is never sent.
    function commitUnit(): void {
        if (correct("unit_number", unitDraft, unitSaved))
            unitSaved = unitDraft.trim();
    }

    function commitOther(): void {
        if (otherOpen && correct("location", otherDraft, otherSaved))
            otherSaved = otherDraft.trim();
    }

    function commitVersion(): void {
        if (correct("software_version", versionDraft, versionSaved))
            versionSaved = versionDraft.trim();
    }

    // Saves every field that is valid and not saved yet. The shell calls it too when it leaves the screen another way.
    function commitAll(): void {
        commitUnit();
        commitOther();
        commitVersion();
    }

    function locationIsConfigured(name: string): bool {
        for (let i = 0; i < locationRepeater.count; ++i) {
            const row = locationRepeater.itemAt(i) as ListRow;
            if (row !== null && row.text === name)
                return true;
        }
        return false;
    }

    // Takes the values from the model. A field that is being typed in keeps what is typed, unless `all` is set (the screen
    // has just opened, or a new log has begun).
    function refresh(all: bool): void {
        if (all || !unitField.hasFocus) {
            if (unitDraft !== unitDigits())
                unitTouched = false;
            unitDraft = unitDigits();
            unitSaved = unitDraft;
        }
        if (all || !versionField.hasFocus) {
            if (versionDraft !== session.versionText)
                versionTouched = false;
            versionDraft = session.versionText;
            versionSaved = versionDraft;
        }
        const place = session.locationText;
        if (all)
            otherOpen = false;
        if (place.length > 0 && !locationIsConfigured(place)) {
            otherOpen = true;
            if (all || !otherField.hasFocus) {
                otherDraft = place;
                otherSaved = place;
            }
        } else if (all) {
            otherDraft = "";
            otherSaved = "";
            otherTouched = false;
        }
    }

    function chooseLocation(locationId: string): void {
        otherOpen = false;
        otherTouched = false;
        session.correctHeader("location", locationId);
    }

    // Scrolls so that the field being typed in is not under the keyboard.
    function reveal(item: Item): void {
        if (item === null)
            return;
        const top = item.mapToItem(column, 0, 0).y;
        const bottom = top + item.height + Theme.space.sectionGap;
        if (top < scroller.contentY)
            scroller.contentY = Math.max(0, top - Theme.space.sectionGap);
        else if (bottom > scroller.contentY + scroller.height)
            scroller.contentY = Math.min(Math.max(0, scroller.contentHeight - scroller.height), bottom - scroller.height);
    }

    function fieldFocusChanged(field: Item, focused: bool): void {
        if (focused) {
            activeField = field;
            reveal(field);
        } else if (field === unitField) {
            commitUnit();
        } else if (field === otherField) {
            commitOther();
        } else if (field === versionField) {
            commitVersion();
        }
    }

    function leave(): void {
        commitAll();
        closeRequested();
    }

    onKeyboardHeightChanged: Qt.callLater(() => root.reveal(root.activeField))
    Component.onCompleted: refresh(true)

    Connections {
        target: root.session

        function onHeaderChanged(): void {
            root.refresh(false);
        }

        function onNewLogStarted(): void {
            root.refresh(true);
        }
    }

    Connections {
        target: Application

        function onStateChanged(): void {
            if (Application.state !== Qt.ApplicationActive)
                root.commitAll();
        }
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.colour.background
        Accessible.ignored: true
    }

    SafeFrame {
        id: frame

        anchors.fill: parent
        overrideInsets: root.overrideInsets
        overrideTop: root.overrideTop
        overrideBottom: root.overrideBottom

        NavBar {
            id: nav

            width: parent.width
            title: qsTr("Today's details")
            large: true
            leadingText: qsTr("Log")
            leadingAccessibleName: qsTr("Back to Log")
            leadingBack: true
            onLeadingTriggered: root.leave()
        }

        Flickable {
            id: scroller

            objectName: "headerScroller"
            y: nav.height
            width: parent.width
            // Down to the foot bar, or to the keyboard when that is up and the bar is hidden.
            height: Math.max(0, (footer.visible ? footer.y : parent.height - Math.max(0, root.keyboardHeight - frame.bottomInset)) - nav.height)
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
                    color: Theme.colour.textSecondary
                    font: Theme.font.bodySmall.font
                    text: qsTr("Set these once at the start of the day. They head your log.")
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.StaticText
                    Accessible.name: text
                }

                GroupedList {
                    width: parent.width

                    ListRow {
                        objectName: "headerFacilitator"
                        text: qsTr("Facilitator")
                        value: root.session.facilitator
                        tappable: false
                    }

                    ListRow {
                        objectName: "headerDate"
                        text: qsTr("Date")
                        value: root.session.dateText
                        tappable: false
                    }
                }

                FormField {
                    id: unitField

                    objectName: "headerUnit"
                    label: qsTr("%1 number").arg(root.settings.unitPrefix)
                    prefixText: root.unitLead
                    text: root.unitDraft
                    hint: qsTr("Type the number only.")
                    errorText: root.unitError
                    valid: root.unitDraft.length > 0 && root.unitError.length === 0
                    inputMethodHints: Qt.ImhDigitsOnly
                    onTextEdited: text => {
                        root.unitDraft = text;
                        root.unitTouched = true;
                    }
                    onAccepted: root.commitUnit()
                    onHasFocusChanged: root.fieldFocusChanged(unitField, hasFocus)
                }

                GroupedList {
                    objectName: "headerLocations"
                    width: parent.width
                    title: qsTr("Location")

                    Repeater {
                        id: locationRepeater

                        model: root.settings.locations

                        ListRow {
                            id: locationRow

                            required property string name
                            required property string locationId

                            objectName: "headerLocation"
                            text: locationRow.name
                            checkable: true
                            checked: !root.otherOpen && root.session.locationText === locationRow.name
                            onActivated: root.chooseLocation(locationRow.locationId)
                        }
                    }

                    ListRow {
                        objectName: "headerOther"
                        text: qsTr("Other…")
                        checkable: true
                        checked: root.otherOpen
                        onActivated: {
                            root.otherOpen = true;
                            otherField.focusField();
                        }
                    }
                }

                FormField {
                    id: otherField

                    objectName: "headerOtherText"
                    visible: root.otherOpen
                    height: visible ? implicitHeight : 0
                    label: qsTr("Other location")
                    text: root.otherDraft
                    placeholderText: qsTr("Where are you?")
                    errorText: root.otherError
                    valid: root.otherDraft.trim().length > 0 && root.otherError.length === 0
                    onTextEdited: text => {
                        root.otherDraft = text;
                        root.otherTouched = true;
                    }
                    onAccepted: root.commitOther()
                    onHasFocusChanged: root.fieldFocusChanged(otherField, hasFocus)
                }

                FormField {
                    id: versionField

                    objectName: "headerVersion"
                    label: qsTr("Software version")
                    text: root.versionDraft
                    placeholderText: qsTr("For example 1.4.2")
                    hint: qsTr("Format 1.4.2 – numbers only.")
                    errorText: root.versionError
                    valid: root.versionDraft.length > 0 && root.versionError.length === 0
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onTextEdited: text => {
                        root.versionDraft = text;
                        root.versionTouched = true;
                    }
                    onAccepted: root.commitVersion()
                    onHasFocusChanged: root.fieldFocusChanged(versionField, hasFocus)
                }

                GroupedList {
                    visible: root.session.versionText.length > 0
                    width: parent.width

                    ListRow {
                        objectName: "headerChangeVersion"
                        text: qsTr("The version changed during the day")
                        showChevron: true
                        onActivated: versionSheet.show()
                    }
                }

                GroupedList {
                    objectName: "headerNewLog"
                    width: parent.width
                    title: qsTr("Another unit or site")
                    footer: qsTr("Keeps today's log in History and opens an empty one. Only the facilitator carries over.")

                    ListRow {
                        objectName: "headerNewLogRow"
                        text: qsTr("Start a new log for another unit or site")
                        showChevron: true
                        onActivated: newLogAlert.open()
                    }
                }
            }
        }

        Item {
            id: footer

            visible: root.keyboardHeight === 0
            y: parent.height - height
            width: parent.width
            height: startButton.height + note.height + 2 * Theme.space.tileGap + Theme.space.sectionGap

            PrimaryButton {
                id: startButton

                objectName: "headerStart"
                x: Theme.space.screenMargin
                y: Theme.space.tileGap
                width: parent.width - 2 * Theme.space.screenMargin
                text: qsTr("Start logging")
                onClicked: root.leave()
            }

            Text {
                id: note

                x: Theme.space.screenMargin
                y: startButton.y + startButton.height + Theme.space.tileGap
                width: parent.width - 2 * Theme.space.screenMargin
                horizontalAlignment: Text.AlignHCenter
                color: Theme.colour.textSecondary
                font: Theme.font.caption.font
                text: qsTr("You can log errors before every field is filled.")
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }

    sheets: [
        VersionSheet {
            id: versionSheet

            objectName: "versionSheet"
            session: root.session
            timeline: root.timeline
            settings: root.settings
        },
        Alert {
            id: newLogAlert

            objectName: "newLogAlert"
            title: qsTr("Start a new log?")
            message: root.newLogMessage
            icon: Alert.Calendar
            confirmText: qsTr("Start a new log")
            cancelText: qsTr("Cancel")
            onConfirmed: root.session.startNewLog()
        }
    ]
}
