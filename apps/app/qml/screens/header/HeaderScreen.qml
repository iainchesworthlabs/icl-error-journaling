// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// Today's details (Stage 4, package 4.9; design `Header.dc.html`, `HeaderNewLogRow.dc.html` and
// `StartNewLogConfirm.dc.html`; HDR-01 to HDR-07 and DAY-02): the facilitator and the date, read only, then the three
// fields of the day's header, then the way to start a new log, and "Start logging" at the foot.
//
//   unit number       a fixed prefix the person does not type, and the number pad. The number is checked as it is typed
//                     with `session.validateUnit`, and a valid one is saved with `session.correctUnit`
//   environment       optional (PRF-11), picked from a short list and never typed: the "Environment" row under the unit field
//                     shows None, prod, dev or whatever the setup lists, and opens the Environment sheet. It is saved with
//                     the number, as "UNIT-27-prod"; chosen before a valid number is typed, it waits for the number
//   location          the configured locations as a list of choices, and Other with a text field
//   software version  a field checked the same way. A first fill, and a typo, are a Correct (HDR-07); a version that
//                     changed during the day is a Change, which the Software version sheet records. The design draws no row
//                     for it, so the sheet opens from the check mark of the version field (its `accessory`), which is a
//                     button, named "The version changed during the day", while the field shows the version in force
//
// Nothing here blocks logging (HDR-06): "Start logging" only closes the screen, and an empty or refused field is left as
// it was. The screen holds what is typed, and the model holds what is saved; a value the model refuses is never sent.
//
// The last row, "Start a new log for another unit or site" (decision 8), asks first, because it archives the open log: the
// alert says how many entries the log has (`session.liveEntryCount`), and that the log has not been shared while
// `session.unshared` (never shared, or shared and changed since: `session.changedSinceShare` says which), with "Share this log first" (`share.shareOpenLog()`; the alert comes back when the share has ended,
// without the button once the share is recorded). Then `session.startNewLog()` runs.
//
// The title is a large one that collapses to the compact bar once the page has scrolled (HeaderNewLogRow): the page
// scrolls under the bar, which draws its own background, and `nav.scrollPosition` is how far it has gone.
//
// The screen is a SheetHost, so the Software version sheet, its time sheet and the alert cover it, and it needs a window.
// The shell hands it the models and listens to `closeRequested`.
SheetHost {
    id: root

    required property var session
    required property var settings
    // For the time of the version sheet (`formatLocalSeconds`); the screen does not use it itself.
    required property var timeline
    // The ShareService, for "Share this log first"; null where the screen has none (a test), and the button is not offered.
    property var share: null

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
    // The environment as picked ("" for None) and as the model was last given it.
    property string environmentDraft: ""
    property string environmentSaved: ""
    property bool otherOpen: false
    property string otherDraft: ""
    property string otherSaved: ""
    property bool otherTouched: false
    property string versionDraft: ""
    property string versionSaved: ""
    property bool versionTouched: false

    readonly property string unitLead: qsTr("%1-").arg(settings.unitPrefix)
    readonly property string unitError: unitTouched ? session.validateUnit(unitDraft, environmentDraft) : ""
    readonly property string environmentLabel: environmentDraft.length > 0 ? environmentDraft : qsTr("None")
    // What is under the Environment row: nothing, unless an environment was picked before there is a number to save it with.
    readonly property string environmentNote: environmentDraft.length > 0 && unitSaved.length === 0 ? qsTr("Saved with the number, once you have typed it.") : ""
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
        return qsTr("Today’s log");
    }
    readonly property bool canShareFirst: session.unshared && share !== null && share.available
    readonly property string newLogMessage: session.changedSinceShare ? qsTr("%1 has %2 and has changed since it was last shared. It stays in History. The new log starts empty, with %3 as facilitator.").arg(logLabel).arg(entriesText).arg(session.facilitator) : session.unshared ? qsTr("%1 has %2 and hasn’t been shared. It stays in History. The new log starts empty, with %3 as facilitator.").arg(logLabel).arg(entriesText).arg(session.facilitator) : qsTr("%1 has %2. It stays in History. The new log starts empty, with %3 as facilitator.").arg(logLabel).arg(entriesText).arg(session.facilitator)
    // The field being typed in, kept in view above the keyboard.
    property Item activeField: null
    // The height of the collapsed bar: its row and the divider under it.
    readonly property real compactBar: Theme.size.minTouchTarget + Theme.border.hairline
    // The height of the bar as it is at rest, large: the page leaves that room at the top (`topMargin`) and scrolls under the
    // bar, whether the bar is large or collapsed. It follows the bar while the bar is large.
    property real restBarHeight: 0

    // The digits of the unit number in force: the model gives "UNIT-27" and the screen shows the prefix apart.
    function unitDigits(): string {
        return session.unitDigits;
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
        const digits = unitDraft.trim();
        if (digits === unitSaved && environmentDraft === environmentSaved)
            return;
        if (session.validateUnit(digits, environmentDraft).length > 0)
            return;
        if (session.correctUnit(digits, environmentDraft)) {
            unitSaved = digits;
            environmentSaved = environmentDraft;
        }
    }

    // The environment was picked on the sheet: "" is None. It is saved with the number when there is a valid one.
    function chooseEnvironment(environment: string): void {
        environmentDraft = environment;
        commitUnit();
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
        // The environment belongs to the saved unit. Until there is one, what was picked stays picked.
        if (all || session.unitDigits.length > 0) {
            environmentDraft = session.unitEnvironment;
            environmentSaved = environmentDraft;
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

    // Scrolls so that the field being typed in is not under the keyboard, nor under the bar. The page scrolls under the bar,
    // and `contentY` is -topMargin at rest, so a position is a content coordinate and the bar covers the top `nav.height` of
    // what is shown. The bar is collapsed (compact height) as soon as the page has scrolled.
    function reveal(item: Item): void {
        if (item === null)
            return;
        const top = item.mapToItem(scroller.contentItem, 0, 0).y;
        const bottom = top + item.height + Theme.space.sectionGap;
        const restY = -scroller.topMargin;
        const shownTop = scroller.contentY + nav.height;
        if (top - Theme.space.sectionGap < shownTop) {
            const target = top - Theme.space.sectionGap - compactBar;
            scroller.contentY = target <= restY ? restY : target;
        } else if (bottom > scroller.contentY + scroller.height) {
            scroller.contentY = Math.min(Math.max(restY, scroller.contentHeight - scroller.height), bottom - scroller.height);
        }
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
    Component.onCompleted: {
        root.restBarHeight = nav.implicitHeight;
        refresh(true);
    }

    Connections {
        function onImplicitHeightChanged(): void {
            if (!nav.collapsed)
                root.restBarHeight = nav.implicitHeight;
        }

        target: nav
    }

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

        // The page scrolls under the bar, so the bar has a background of the screen's colour behind it.
        Rectangle {
            z: 1
            width: parent.width
            height: nav.height
            color: Theme.colour.background
            Accessible.ignored: true
        }

        NavBar {
            id: nav

            objectName: "headerNav"
            z: 2
            width: parent.width
            title: qsTr("Today’s details")
            large: true
            scrollPosition: scroller.contentY + scroller.topMargin
            leadingText: qsTr("Log")
            leadingAccessibleName: qsTr("Back to Log")
            leadingBack: true
            onLeadingTriggered: root.leave()
        }

        Flickable {
            id: scroller

            objectName: "headerScroller"
            width: parent.width
            // Down to the foot bar, or to the keyboard when that is up and the bar is hidden.
            height: Math.max(0, footer.visible ? footer.y : parent.height - Math.max(0, root.keyboardHeight - frame.bottomInset))
            clip: true
            topMargin: root.restBarHeight
            contentWidth: width
            contentHeight: column.height + Theme.space.sectionGap
            boundsBehavior: Flickable.StopAtBounds
            // The page rests with its top under the bar, which is `topMargin` when it is not scrolled; a margin that changes (the
            // text size, a line that wraps) moves a page that is at rest with it.
            property real restMargin: 0

            onTopMarginChanged: {
                if (contentY <= -restMargin + Theme.border.hairline)
                    contentY = -topMargin;
                restMargin = topMargin;
            }

            Column {
                id: column

                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                spacing: Theme.space.sectionGap + Theme.space.tileGap

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
                    valueFont: Theme.font.fieldValueLarge.font
                    hint: qsTr("Type the number only.")
                    errorText: root.unitError
                    inputMethodHints: Qt.ImhDigitsOnly
                    onTextEdited: text => {
                        root.unitDraft = text;
                        root.unitTouched = true;
                    }
                    onAccepted: root.commitUnit()
                    onHasFocusChanged: root.fieldFocusChanged(unitField, hasFocus)
                }

                GroupedList {
                    objectName: "headerEnvironmentGroup"
                    width: parent.width
                    footer: root.environmentNote

                    ListRow {
                        objectName: "headerEnvironment"
                        text: qsTr("Environment")
                        value: root.environmentLabel
                        showChevron: true
                        onActivated: environmentSheet.show(root.environmentDraft)
                    }
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
                    valueFont: Theme.font.fieldValueLarge.font
                    placeholderText: qsTr("For example 1.4.2")
                    hint: qsTr("Format 1.4.2 – numbers only.")
                    errorText: root.versionError
                    valid: root.versionDraft.length > 0 && root.versionError.length === 0
                    // The check mark is the way to the Software version sheet while the field shows the version in force.
                    accessoryName: root.session.versionText.length > 0 && root.versionDraft === root.session.versionText ? qsTr("The version changed during the day") : ""
                    onAccessoryTriggered: versionSheet.show()
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onTextEdited: text => {
                        root.versionDraft = text;
                        root.versionTouched = true;
                    }
                    onAccepted: root.commitVersion()
                    onHasFocusChanged: root.fieldFocusChanged(versionField, hasFocus)
                }

                // The new-log group sits 8 pt further from the version field than the other groups (HeaderNewLogRow).
                Item {
                    width: parent.width
                    height: newLogGroup.height + Theme.space.tileGap

                    GroupedList {
                        id: newLogGroup

                        objectName: "headerNewLog"
                        y: Theme.space.tileGap
                        width: parent.width
                        title: qsTr("Another unit or site")
                        footer: qsTr("Keeps today’s log in History and opens an empty one. Only the facilitator carries over.")

                        ListRow {
                            objectName: "headerNewLogRow"
                            text: qsTr("Start a new log for another unit or site")
                            leadingIcon: "plus"
                            leadingIconStroke: Theme.border.hairline * 2.2
                            labelColour: Theme.colour.primaryText
                            minimumHeight: Theme.size.rowHeightAction
                            showChevron: true
                            onActivated: newLogAlert.open()
                        }
                    }
                }
            }
        }

        Item {
            id: footer

            // The button and its note are 10 pt apart, and the note ends at the bottom of the safe area. Once the page has
            // scrolled the note is gone and the button sits at the bottom of the safe area (HeaderNewLogRow); the foot keeps
            // its height, so the page above it does not change size under the finger.
            readonly property real noteGap: Theme.space.tileGap + Theme.border.hairline * 2

            visible: root.keyboardHeight === 0
            y: parent.height - height
            width: parent.width
            height: Theme.space.tileGap + startButton.height + noteGap + note.height

            // The foot has the screen's colour, so that nothing of the page shows through between the button and its note.
            Rectangle {
                anchors.fill: parent
                color: Theme.colour.background
                Accessible.ignored: true
            }

            PrimaryButton {
                id: startButton

                objectName: "headerStart"
                x: Theme.space.screenMargin
                y: nav.collapsed ? footer.height - height : Theme.space.tileGap
                width: parent.width - 2 * Theme.space.screenMargin
                text: qsTr("Start logging")
                onClicked: root.leave()
            }

            Text {
                id: note

                visible: !nav.collapsed
                x: Theme.space.screenMargin
                y: startButton.y + startButton.height + footer.noteGap
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
        EnvironmentSheet {
            id: environmentSheet

            objectName: "environmentSheet"
            settings: root.settings
            onChosen: environment => root.chooseEnvironment(environment)
        },
        VersionSheet {
            id: versionSheet

            objectName: "versionSheet"
            session: root.session
            timeline: root.timeline
            settings: root.settings
        },
        // The time of a change, over the version sheet: declared after it, so that it stacks over it.
        VersionTimeSheet {
            objectName: "versionTimeSheet"
            version: versionSheet
            settings: root.settings
        },
        Alert {
            id: newLogAlert

            objectName: "newLogAlert"
            title: qsTr("Start a new log?")
            message: root.newLogMessage
            icon: Alert.Calendar
            confirmText: qsTr("Start a new log")
            alternateText: root.canShareFirst ? qsTr("Share this log first") : ""
            cancelText: qsTr("Cancel")
            onConfirmed: root.session.startNewLog()
            // The share sheet opens over the screen; the alert is up again under it, as the new-day alert is, and has no
            // "Share this log first" once the share is recorded.
            onAlternated: {
                root.share.shareOpenLog();
                newLogAlert.open();
            }
        }
    ]
}
