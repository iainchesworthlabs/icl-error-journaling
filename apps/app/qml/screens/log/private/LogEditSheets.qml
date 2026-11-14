// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The Edit entry sheet and its time sheet, for the Log screen (BRD-07): a hold on a tile with an empty composer logs the error and
// opens this on the new entry with the cursor in the note (`editor.editNote(entryId)`). They are the sheets of Review, the same
// files, over the same timeline, so Save attaches the note as an edit there does (NOTE-06), Cancel leaves the entry as it was and
// "Delete entry" works. The time sheet is declared after the Edit entry sheet, so it stacks over it.
//
// The Log screen makes this at the first hold, in a Loader that fills the sheet layer, and asks `isOpen` of it to tell the layer's
// host that a sheet is up.
Item {
    id: root

    required property var timeline
    required property var picker
    required property var settings
    // The list of people for the people of an interaction (PeopleModel); null where there is none.
    property var people: null

    readonly property alias editor: entryEditor
    readonly property bool isOpen: entryEditor.isOpen || entryTimeSheet.isOpen

    anchors.fill: parent

    EditEntry {
        id: entryEditor

        timeline: root.timeline
        picker: root.picker
        settings: root.settings
        people: root.people
    }

    EditTimeSheet {
        id: entryTimeSheet

        edit: entryEditor
        timeline: root.timeline
        settings: root.settings
    }
}
