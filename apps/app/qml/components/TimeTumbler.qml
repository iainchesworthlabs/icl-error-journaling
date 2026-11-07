// SPDX-License-Identifier: GPL-3.0-or-later
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

    readonly property bool invalid: errorText.length > 0
    // The time the columns show, in seconds since midnight. The tumbler keeps it; a screen reads `localSeconds`.
    property int shownSeconds: localSeconds

    // The user changed the time.
    signal timeEdited(int localSeconds)

    property bool syncing: false

    // A row of a column is 44 pt at least and grows with the text; five rows are shown, the middle one selected.
    readonly property real rowHeight: Math.max(Theme.size.minTouchTarget, Theme.font.headerUnit.size * 1.5)
    readonly property int visibleCount: 5
    readonly property real rowsHeight: rowHeight * visibleCount
    // The width of a column: 88 pt, or less when the 12-hour format with seconds has four of them to fit.
    readonly property int columnCount: 2 + (showSeconds ? 1 : 0) + (twelveHour ? 1 : 0)
    readonly property int separatorCount: showSeconds ? 2 : 1
    // The colon between two columns: a gap wide, or as wide as the colon in the font if that is more.
    readonly property real separatorWidth: Math.max(Theme.space.sectionGap, colonMetrics.advanceWidth)
    // The gaps of the row of columns and colons.
    readonly property int gapCount: columnCount + separatorCount - 1
    readonly property real columnWidth: Math.min(Theme.size.minTouchTarget * 2, (width - 2 * Theme.space.tileGap - separatorCount * separatorWidth - gapCount * columns.spacing) / columnCount)

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: card.height + message.height
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

                readonly property bool selected: Math.abs(Tumbler.displacement) < 0.5

                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: selected ? Theme.colour.textPrimary : Theme.colour.textSecondary
                font: selected ? Theme.font.headerUnit.font : Theme.font.body.font
                text: String(modelData)
                Accessible.ignored: true
            }
        }
    }

    TextMetrics {
        id: colonMetrics

        font: Theme.font.headerUnit.font
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

        Text {
            width: parent.width
            y: parent.headingHeight + parent.rowsHeight / 2 - height / 2
            horizontalAlignment: Text.AlignHCenter
            color: Theme.colour.textPrimary
            font: Theme.font.headerUnit.font
            text: ":"
            Accessible.ignored: true
        }
    }

    Rectangle {
        id: card

        width: parent.width
        height: columns.height + 2 * Theme.space.tileGap + Theme.space.sectionGap
        radius: Theme.radius.groupedList
        color: Theme.colour.surface

        // The selection band behind the middle row.
        Rectangle {
            x: Theme.space.tileGap
            y: Theme.space.sectionGap + hours.headingHeight + root.rowHeight * Math.floor(root.visibleCount / 2)
            width: parent.width - 2 * Theme.space.tileGap
            height: root.rowHeight
            radius: Theme.radius.field
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
                columnWidth: root.columnWidth
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
}
