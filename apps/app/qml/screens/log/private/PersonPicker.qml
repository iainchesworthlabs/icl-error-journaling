// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The person picker (Stage 14, package I-6; docs/plan/stage-14.md, 4.4): the sheet that opens after an interaction that
// has a [person] slot has been logged. The entry is in the log already (log first, ask second), so nothing here can lose
// it: Skip leaves it as it was, and Done (n) puts the ticked people on it in one edit.
//
// A name is typed once and picked later. The search field filters the list; when what is typed matches nobody the first
// row is `Add "..." as a new person`, and when it matches somebody by case, spacing or accents that person is shown and
// no duplicate can be made. A new person is saved first and ticked when the save has committed (`personAdded`), so the
// edit of the entry never refers to a name the store does not have.
//
// Tapping a row ticks or unticks it (up to 20 people). "Everyone here" ticks the people of the location in force.
//
//   people       PeopleModel: the rows (personId, name, locationId), `available`, `matchFor`, `addPerson`, `everyoneAt`
//   locationId   the location of the log in force, "" for none: the Everyone row is shown only when it has people
//   typeName     the interaction type, in the title
//
// Signals: `doneRequested(personIds)` for Done (n), `skipped()` for Skip and every other way of leaving (the scrim, the
// grabber).
Sheet {
    id: root

    required property var people
    property string locationId: ""
    property string typeName: ""

    // The people ticked, in the order they were picked.
    property var picked: []
    // The sentence under the search field: the limit, or why a name was not added.
    property string notice: ""
    // The name that was sent to be added and is waiting for the save: it is ticked when it comes back.
    property bool adding: false
    readonly property int maximum: 20
    readonly property string filterText: search.filter.trim()
    readonly property string matchId: filterText.length > 0 ? root.people.matchFor(filterText) : ""
    readonly property bool offersAdd: filterText.length > 0 && matchId.length === 0 && root.people.available
    readonly property var everyoneIds: locationId.length > 0 ? root.people.everyoneAt(locationId) : []
    // The picker edits the people of an entry that has some already (the Edit entry sheet): the people of the entry are ticked,
    // there is no Skip but Cancel, and Done with nobody ticked takes everyone off the entry.
    property bool editing: false
    // The people ticked that the list does not show (retired, or forgotten), so that they can be taken off.
    readonly property var unlisted: picked.filter(id => !root.people.isListed(id))
    // Done was chosen: the sheet closing is then not a skip.
    property bool finished: false

    signal doneRequested(var personIds)
    signal skipped

    function isPicked(personId: string): bool {
        return picked.indexOf(personId) >= 0;
    }

    function toggle(personId: string): void {
        const next = picked.slice();
        const at = next.indexOf(personId);
        if (at >= 0) {
            next.splice(at, 1);
            notice = "";
        } else if (next.length >= maximum) {
            notice = qsTr("An interaction can name up to %1 people.").arg(maximum);
            return;
        } else {
            next.push(personId);
            notice = "";
        }
        picked = next;
    }

    function tickEveryone(): void {
        const next = picked.slice();
        for (const id of everyoneIds) {
            if (next.indexOf(id) < 0 && next.length < maximum)
                next.push(id);
        }
        notice = next.length >= maximum && everyoneIds.length > next.length ? qsTr("An interaction can name up to %1 people.").arg(maximum) : "";
        picked = next;
    }

    function addTyped(): void {
        if (root.matchId.length > 0) {
            toggle(root.matchId);
            search.filter = "";
            return;
        }
        adding = true;
        if (!root.people.addPerson(root.filterText, root.locationId))
            adding = false;
    }

    // Opens the picker. `picks` (ordered person ids) are ticked and make it an editor of those people.
    function begin(picks): void {
        editing = picks !== undefined;
        picked = editing ? picks.slice() : [];
        notice = "";
        adding = false;
        finished = false;
        search.filter = "";
        open();
    }

    function finish(): void {
        finished = true;
        doneRequested(picked);
        close();
    }

    accessibleName: qsTr("Pick people")
    contentUnderHomeIndicator: true
    onOpened: search.focusField()
    onClosed: {
        if (!root.finished)
            root.skipped();
    }

    Connections {
        target: root.people

        function onPersonAdded(personId: string): void {
            if (!root.adding)
                return;
            root.adding = false;
            root.toggle(personId);
            search.filter = "";
        }

        function onAddFailed(message: string): void {
            root.adding = false;
            root.notice = message;
        }
    }

    NavBar {
        id: nav

        objectName: "personPickerBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: root.typeName.length > 0 ? qsTr("%1 …").arg(root.typeName) : qsTr("Pick people")
        inSheet: true
        leadingText: root.editing ? qsTr("Cancel") : qsTr("Skip")
        trailingText: root.picked.length > 0 ? qsTr("Done (%1)").arg(root.picked.length) : qsTr("Done")
        onLeadingTriggered: root.close()
        onTrailingTriggered: root.finish()
    }

    SearchField {
        id: search

        objectName: "personPickerSearch"
        anchors.top: nav.bottom
        anchors.topMargin: -Theme.space.tileGap * 3 / 4
        x: Theme.space.screenMargin
        width: parent.width - 2 * Theme.space.screenMargin
        placeholderText: qsTr("Search or type a name")
        accessibleName: qsTr("Search or type a name")
        onFilterEdited: text => search.filter = text
        onAccepted: {
            if (root.offersAdd || root.matchId.length > 0)
                root.addTyped();
        }
    }

    Flickable {
        id: flick

        objectName: "personPickerList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: search.bottom
        anchors.topMargin: Theme.space.sectionGap
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin + root.contentBottomInset
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.sectionGap

            Text {
                objectName: "personPickerNotice"
                width: parent.width
                visible: root.notice.length > 0
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: root.notice
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            Text {
                objectName: "personPickerUnavailable"
                width: parent.width
                visible: !root.people.available
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("People are not available right now. Tap Skip to leave this interaction as it is.")
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            GroupedList {
                width: parent.width
                surface: Theme.colour.surfaceRaised
                visible: root.offersAdd || (root.everyoneIds.length > 0 && root.filterText.length === 0)

                ListRow {
                    objectName: "personPickerAdd"
                    visible: root.offersAdd
                    text: qsTr("Add “%1” as a new person").arg(root.filterText)
                    leadingIcon: "plus"
                    labelColour: Theme.colour.primaryText
                    minimumHeight: Theme.size.rowHeightForm
                    onActivated: root.addTyped()
                }

                ListRow {
                    objectName: "personPickerEveryone"
                    visible: root.everyoneIds.length > 0 && root.filterText.length === 0
                    text: qsTr("Everyone here")
                    value: qsTr("%n people", "", root.everyoneIds.length)
                    minimumHeight: Theme.size.rowHeightForm
                    onActivated: root.tickEveryone()
                }
            }

            GroupedList {
                objectName: "personPickerUnlisted"
                width: parent.width
                surface: Theme.colour.surfaceRaised
                visible: root.unlisted.length > 0
                title: qsTr("On this entry")

                Repeater {
                    model: root.unlisted

                    ListRow {
                        id: kept

                        required property string modelData

                        objectName: "personPickerUnlistedRow"
                        text: root.people.labelFor(kept.modelData)
                        checkable: true
                        checked: true
                        minimumHeight: Theme.size.rowHeightForm
                        onActivated: root.toggle(kept.modelData)
                    }
                }
            }

            GroupedList {
                id: listed

                width: parent.width
                surface: Theme.colour.surfaceRaised
                visible: root.people.count > 0

                Repeater {
                    model: root.people

                    ListRow {
                        id: row

                        required property string personId
                        required property string name

                        objectName: "personPickerRow"
                        readonly property bool matches: root.filterText.length === 0 || row.name.toLocaleLowerCase().indexOf(root.filterText.toLocaleLowerCase()) >= 0 || root.matchId === row.personId
                        visible: matches
                        height: matches ? implicitHeight : 0
                        text: row.name
                        checkable: true
                        checked: root.isPicked(row.personId)
                        minimumHeight: Theme.size.rowHeightForm
                        onActivated: root.toggle(row.personId)
                    }
                }
            }

            Text {
                objectName: "personPickerEmpty"
                width: parent.width
                visible: root.people.available && root.people.count === 0 && root.filterText.length === 0
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("Type a name to add the first person. You pick it from this list next time.")
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }
        }
    }
}
