// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The time input of the Edit entry and Software version sheets (decision 11 of docs/plan/stage-4.md): a column for the
// hours and one for the minutes, with a column for the seconds and one for AM or PM when the time format has them. Each
// column is a Basic Tumbler with its look replaced. A text field would invite a typo that the model must refuse.
//
// It shows and gives local time only: `localSeconds` is seconds since local midnight (0 to 86399), and it is the screen
// that turns it into an instant (`TimelineModel::localToUtc`, which refuses the hour the clocks skip). The screen then
// puts the refusal in `errorText` and the tumbler shows it under the columns, in the destructive colour, and says it with
// the group. The screen also shows the time as the log will print it (`formatLocalSeconds`); the tumbler formats
// nothing but its own column numbers.
//
// `localSeconds` is set by the screen and `timeEdited(localSeconds)` is emitted once for each change the user makes (a
// notch of a column, a key, or a VoiceOver adjustment). The tumbler never assigns `localSeconds`, so a binding on it is
// never broken. A new value from outside moves the columns without emitting. With the seconds column hidden the seconds
// of the value are kept.
//
// Look (TimeTumbler, TimeTumbler12): the rows are 44 pt, the unselected ones 20/400 in textSecondary and the one in the
// selection band 24/600 in textPrimary, with tabular figures; the band has a 10 pt radius; the card has 12 pt above the
// columns and 8 pt below, and its fill is `surface` (surfaceRaised by default, as the tumbler sits on a sheet). In the
// 24-hour format the columns are 88 pt; in the 12-hour format they are 72 pt and AM/PM 64 pt, with 8 pt colons.
//
// Optional line under the card (12 pt below it, 10 pt apart): `nowText` shows the "Now" chip (44 pt, 18 pt side padding,
// a 12 pt radius, primaryTint, 16/600 primaryText), which emits `nowRequested()`; `caption` is the 14 pt text beside it
// ("Shown in the log as 11:02"). Both are empty by default, and the line is not drawn.
//
// VoiceOver. Each column is an adjustable item (a spin box): swipe up or down to change it by one, and it reads its name
// and its value.
Item {
    id: root

    required property int localSeconds
    // The time format: 12-hour with AM/PM, and the seconds column.
    property bool twelveHour: false
    property bool showSeconds: false
    // The message of the model when the time is refused; empty when it is not.
    property string errorText: ""
    property string accessibleName: qsTr("Time")
    property string hoursLabel: qsTr("Hours")
    property string minutesLabel: qsTr("Minutes")
    property string secondsLabel: qsTr("Seconds")
    property string meridiemLabel: qsTr("AM/PM")
    property var meridiemNames: [qsTr("AM"), qsTr("PM")]
    property color surface: Theme.colour.surfaceRaised
    // The "Now" chip's label; empty for no chip.
    property string nowText: ""
    // The text beside the chip; empty for none.
    property string caption: ""

    readonly property bool invalid: errorText.length > 0
    // The time the columns show, in seconds since midnight. The tumbler keeps it; a screen reads `localSeconds`.
    property int shownSeconds: localSeconds

    // The user changed the time.
    signal timeEdited(int localSeconds)
    // The "Now" chip was tapped: the screen sets the time to now.
    signal nowRequested

    property bool syncing: false

    // A row of a column is 44 pt at least and grows with the text; five rows are shown, the middle one selected.
    readonly property real rowHeight: Math.max(Theme.size.minTouchTarget, Theme.font.headerUnit.size * 1.5)
    readonly property int visibleCount: 5
    readonly property real rowsHeight: rowHeight * visibleCount
    // The width of a column: 88 pt, or less when the 12-hour format with seconds has four of them to fit.
    readonly property int numericColumnCount: 2 + (showSeconds ? 1 : 0)
    readonly property int columnCount: numericColumnCount + (twelveHour ? 1 : 0)
    readonly property int separatorCount: showSeconds ? 2 : 1
    // The colon between two columns: 8 pt in the 12-hour format, otherwise a gap wide or as wide as the colon if more.
    readonly property real separatorWidth: twelveHour ? Theme.space.tileGap : Math.max(Theme.space.sectionGap, colonMetrics.advanceWidth)
    // The gaps of the row of columns and colons.
    readonly property int gapCount: columnCount + separatorCount - 1
    // The widest a column is: 88 pt, or 72 pt in the 12-hour format, where AM/PM is 64 pt.
    readonly property real columnMaximum: twelveHour ? Theme.size.minTouchTarget * 18 / 11 : Theme.size.minTouchTarget * 2
    // AM/PM is 64 pt at the default text size; above it, it takes an even share so that its larger text fits.
    readonly property real evenWidth: (width - 2 * Theme.space.tileGap - separatorCount * separatorWidth - gapCount * columns.spacing) / columnCount
    readonly property real meridiemWidth: Theme.font.tumbler.enlarged ? evenWidth : Math.min(Theme.miniBoard.tile, evenWidth)
    readonly property real columnWidth: Math.min(columnMaximum, (width - 2 * Theme.space.tileGap - separatorCount * separatorWidth - gapCount * columns.spacing - (twelveHour ? meridiemWidth : 0)) / numericColumnCount)
    readonly property bool hasNowLine: nowText.length > 0 || caption.length > 0

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: card.height + message.height + (hasNowLine ? nowLine.height + Theme.space.sectionGap : 0)
    width: parent ? parent.width : implicitWidth

    Accessible.role: Accessible.Grouping
    Accessible.name: root.accessibleName
    Accessible.description: root.errorText

    // ---- the values of the columns ----

    function twoDigits(count: int): var {
        const labels = [];
        for (let i = 0; i < count; ++i)
            labels.push(String(i).padStart(2, "0"));
        return labels;
    }

    function hourLabels(): var {
        if (!twelveHour)
            return twoDigits(24);
        const labels = ["12"];
        for (let i = 1; i < 12; ++i)
            labels.push(String(i));
        return labels;
    }

    // Move the columns to a time, without emitting.
    function show(seconds: int): void {
        const hour = Math.floor(seconds / 3600);
        hours.jumpTo(twelveHour ? hour % 12 : hour);
        minutes.jumpTo(Math.floor((seconds % 3600) / 60));
        secs.jumpTo(seconds % 60);
        meridiem.jumpTo(hour >= 12 ? 1 : 0);
        shownSeconds = seconds;
    }

    // Set the models for the time format and show the time.
    function rebuild(): void {
        syncing = true;
        hours.model = hourLabels();
        minutes.model = twoDigits(60);
        secs.model = twoDigits(60);
        meridiem.model = meridiemNames;
        show(localSeconds);
        syncing = false;
    }

    // A column moved: the time the columns show now.
    function columnsMoved(): void {
        if (syncing)
            return;
        const hour = twelveHour ? meridiem.currentIndex * 12 + hours.currentIndex : hours.currentIndex;
        const seconds = showSeconds ? secs.currentIndex : shownSeconds % 60;
        const total = hour * 3600 + minutes.currentIndex * 60 + seconds;
        if (total === shownSeconds)
            return;
        shownSeconds = total;
        timeEdited(total);
    }

    onLocalSecondsChanged: {
        if (localSeconds === shownSeconds)
            return;
        syncing = true;
        show(localSeconds);
        syncing = false;
    }
    onTwelveHourChanged: rebuild()
    onMeridiemNamesChanged: rebuild()
    Component.onCompleted: rebuild()

    // One column: a small heading over a Tumbler. An inline component cannot see the ids of the file around it, so
    // everything it needs is a property.
    component TimeColumn: Item {
        id: column

        required property string heading
        required property real rowHeight
        required property int visibleCount
        required property real columnWidth
        property bool wrap: true
        readonly property real headingHeight: label.height
        property alias model: tumbler.model
        property alias currentIndex: tumbler.currentIndex

        signal moved

        // Show an index in the middle at once, without the spin that setting `currentIndex` makes.
        function jumpTo(index: int): void {
            tumbler.currentIndex = index;
            tumbler.positionViewAtIndex(index, Tumbler.Center);
        }

        width: columnWidth
        height: label.height + tumbler.height

        Text {
            id: label

            width: parent.width
            height: implicitHeight + Theme.space.tileGap / 2
            horizontalAlignment: Text.AlignHCenter
            color: Theme.colour.textSecondary
            font: Theme.font.sectionLabel.font
            text: column.heading.toLocaleUpperCase()
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }

        Tumbler {
            id: tumbler

            objectName: column.heading
            y: label.height
            width: parent.width
            height: column.rowHeight * column.visibleCount
            visibleItemCount: column.visibleCount
            wrap: column.wrap
            activeFocusOnTab: true

            Accessible.role: Accessible.SpinBox
            Accessible.name: column.heading
            Accessible.description: tumbler.count > 0 ? String(tumbler.model[tumbler.currentIndex]) : ""
            Accessible.focusable: true
            Accessible.onIncreaseAction: tumbler.step(1)
            Accessible.onDecreaseAction: tumbler.step(-1)

            // One notch up or down, as VoiceOver's swipe does.
            function step(delta: int): void {
                const next = tumbler.currentIndex + delta;
                if (tumbler.wrap)
                    tumbler.currentIndex = (next + tumbler.count) % tumbler.count;
                else
                    tumbler.currentIndex = Math.max(0, Math.min(tumbler.count - 1, next));
            }

            onCurrentIndexChanged: column.moved()

            delegate: Text {
                required property var modelData
                required property int index

                // The row in the band: the current index, which the Tumbler moves as the column turns.
                readonly property bool selected: index === tumbler.currentIndex

                objectName: "tumblerRow"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: selected ? Theme.colour.textPrimary : Theme.colour.textSecondary
                font: selected ? Theme.font.tumblerSelected.font : Theme.font.tumbler.font
                text: String(modelData)
                Accessible.ignored: true
            }
        }
    }

    TextMetrics {
        id: colonMetrics

        font: Theme.font.tumblerSelected.font
        text: ":"
    }

    // The colon between two columns, level with the selected row.
    component Separator: Item {
        required property real columnHeight
        required property real headingHeight
        required property real rowsHeight
        required property real separatorWidth

        width: separatorWidth
        height: columnHeight

        // As wide as the colon, centred on the gap: the 8 pt gap of the 12-hour format is narrower than the glyph.
        Text {
            x: (parent.width - width) / 2
            y: parent.headingHeight + parent.rowsHeight / 2 - height / 2
            color: Theme.colour.textPrimary
            font: Theme.font.tumblerSelected.font
            text: ":"
            Accessible.ignored: true
        }
    }

    Rectangle {
        id: card

        width: parent.width
        objectName: "tumblerCard"
        height: columns.height + Theme.space.sectionGap + Theme.space.tileGap
        radius: Theme.radius.groupedList
        color: root.surface

        // The selection band behind the middle row.
        Rectangle {
            x: Theme.space.tileGap
            y: Theme.space.sectionGap + hours.headingHeight + root.rowHeight * Math.floor(root.visibleCount / 2)
            width: parent.width - 2 * Theme.space.tileGap
            objectName: "tumblerBand"
            height: root.rowHeight
            radius: Theme.radius.small
            color: Theme.colour.timeChipBackground
        }

        Row {
            id: columns

            y: Theme.space.sectionGap
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: Theme.space.tileGap / 2

            TimeColumn {
                id: hours

                heading: root.hoursLabel
                rowHeight: root.rowHeight
                visibleCount: root.visibleCount
                columnWidth: root.columnWidth
                onMoved: root.columnsMoved()
            }
            Separator {
                columnHeight: hours.height
                headingHeight: hours.headingHeight
                rowsHeight: root.rowsHeight
                separatorWidth: root.separatorWidth
            }
            TimeColumn {
                id: minutes

                heading: root.minutesLabel
                rowHeight: root.rowHeight
                visibleCount: root.visibleCount
                columnWidth: root.columnWidth
                onMoved: root.columnsMoved()
            }
            Separator {
                visible: root.showSeconds
                columnHeight: hours.height
                headingHeight: hours.headingHeight
                rowsHeight: root.rowsHeight
                separatorWidth: root.separatorWidth
            }
            TimeColumn {
                id: secs

                visible: root.showSeconds
                heading: root.secondsLabel
                rowHeight: root.rowHeight
                visibleCount: root.visibleCount
                columnWidth: root.columnWidth
                onMoved: root.columnsMoved()
            }
            TimeColumn {
                id: meridiem

                visible: root.twelveHour
                heading: root.meridiemLabel
                wrap: false
                rowHeight: root.rowHeight
                visibleCount: root.visibleCount
                columnWidth: root.meridiemWidth
                onMoved: root.columnsMoved()
            }
        }
    }

    Text {
        id: message

        x: Theme.space.tileGap / 2
        y: card.height
        width: parent.width - 2 * x
        height: visible ? implicitHeight + Theme.space.tileGap : 0
        visible: root.invalid
        verticalAlignment: Text.AlignBottom
        color: Theme.colour.destructive
        font: Theme.font.caption.font
        text: root.errorText
        wrapMode: Text.WordWrap
        Accessible.ignored: true
    }

    // The "Now" chip and the caption beside it.
    Item {
        id: nowLine

        objectName: "tumblerNowLine"
        visible: root.hasNowLine
        y: message.y + message.height + Theme.space.sectionGap
        width: parent.width
        height: Math.max(nowChip.visible ? nowChip.height : 0, captionText.implicitHeight)

        Rectangle {
            id: nowChip

            objectName: "tumblerNow"
            visible: root.nowText.length > 0
            width: visible ? nowLabel.implicitWidth + 2 * (Theme.space.screenMargin + Theme.border.hairline * 2) : 0
            height: Math.max(Theme.size.minTouchTarget, nowLabel.implicitHeight + Theme.space.tileGap)
            anchors.verticalCenter: parent.verticalCenter
            radius: Theme.radius.field
            color: nowTap.pressed ? Theme.colour.primaryPressed : Theme.colour.primaryTint
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: root.nowText
            Accessible.focusable: true
            Accessible.onPressAction: root.nowRequested()
            Keys.onReturnPressed: root.nowRequested()
            Keys.onSpacePressed: root.nowRequested()

            Text {
                id: nowLabel

                anchors.centerIn: parent
                color: nowTap.pressed ? Theme.colour.textOnPrimary : Theme.colour.primaryText
                font: Theme.font.body.selectedFont
                text: root.nowText
                Accessible.ignored: true
            }

            TapHandler {
                id: nowTap

                onTapped: root.nowRequested()
            }
        }

        Text {
            id: captionText

            objectName: "tumblerCaption"
            x: nowChip.visible ? nowChip.width + Theme.space.tileGap + Theme.border.hairline * 2 : 0
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - x
            visible: root.caption.length > 0
            color: Theme.colour.textSecondary
            font: Theme.font.messageText.font
            text: root.caption
            wrapMode: Text.WordWrap
        }
    }
}
