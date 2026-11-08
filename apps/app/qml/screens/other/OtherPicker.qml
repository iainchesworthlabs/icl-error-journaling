// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQml.Models
import ErrorJournalingApp

// The Other picker (Stage 4, package 4.8; BRD-06): the sheet the Other tile opens. It lists the error types that are not on
// the board under "Not on your board" and the ones that are under "On your board" with today's counts, narrows both with
// a search field, and adds a new type.
//
// It is a Sheet, so a shell puts it in the `sheets` of a SheetHost and calls `open()`; it closes itself and tells its
// owner through `closed`.
//
//     OtherPicker { picker: appRoot.picker; session: appRoot.session; bridge: appRoot.bridge }
//
// What it binds to:
//   picker   ErrorPickerModel: the rows (roles typeId, label, section, count, colourSlot), `filter` and `addTypeError`,
//            and `addType(name)`, which returns the new id or an empty string when the model refuses the name.
//   session  LogSession: `recordError(typeId)`.
//   bridge   the haptics: `hapticImpact()`.
//
// Choosing a type fires the impact haptic and then records, as a tile does, and closes the sheet; the count, the toast
// and the "Last" bar follow the commit on the Log screen. Adding a type records nothing (Stage 3, decision 12): the type
// appears under "Not on your board" when the save has committed, and the user taps it like any other. A name the model
// refuses stays in the field with the model's message under it.
//
// The sections are two views of the one model, filtered by the `section` role. The screen holds no list of its own and
// no rule about names.
Sheet {
    id: root

    required property var picker
    required property var session
    required property var bridge

    // The values of ErrorPickerModel::Section: the model lists OFF_BOARD first, then ON_BOARD.
    readonly property int offBoardSection: 0
    readonly property int onBoardSection: 1

    // Choosing a type: the haptic first, then the entry, then the sheet closes. The same two calls as a tile's press.
    function pick(typeId: string): void {
        root.bridge.hapticImpact();
        root.session.recordError(typeId);
        root.close();
    }

    // The "Add" button, the Return key in the field, and nothing else: ask the model, and show its refusal or finish.
    function addType(): void {
        const id = root.picker.addType(form.name);
        form.refused = id.length === 0;
        if (!form.refused) {
            form.name = "";
            form.open = false;
            // The new type is listed once the save has committed, so a filter that hides it is cleared.
            root.picker.filter = "";
        }
    }

    // Scrolls to the end of the list, where the add form is, so that it stays in view when the keyboard shortens the sheet.
    function showForm(): void {
        flick.contentY = Math.max(0, flick.contentHeight - flick.height);
    }

    accessibleName: qsTr("Log another error")
    // The list scrolls under the home indicator and pads its end by the inset, instead of being cut above it.
    contentUnderHomeIndicator: true

    onClosed: {
        root.picker.filter = "";
        form.open = false;
        form.name = "";
        form.refused = false;
        flick.contentY = 0;
    }

    // The state of the add form. It is only what the user is doing; the rules are the model's.
    QtObject {
        id: form

        property bool open: false
        property string name: ""
        // The last "Add" was refused and the name has not been edited since, so the model's message still applies.
        property bool refused: false
    }

    SortFilterProxyModel {
        id: offBoard

        model: root.picker
        filters: ValueFilter {
            roleName: "section"
            value: root.offBoardSection
        }
    }

    SortFilterProxyModel {
        id: onBoard

        model: root.picker
        filters: ValueFilter {
            roleName: "section"
            value: root.onBoardSection
        }
    }

    // A row of a group with a mark in front of its text. A ListRow has no place for one, so the row starts after the mark
    // column and the mark, the pressed highlight and the separator of the column are drawn to the left of it, outside the
    // row's own bounds (the mock-up's separator runs the full width of the card).
    component MarkedRow: ListRow {
        id: marked

        // The width of the mark: 14 pt for a swatch, 20 pt for the plus sign.
        property real markWidth: Theme.space.rowPadding
        // The mark column: the swatch or the plus sign is in the padding of the row, and the text starts 12 pt after it.
        readonly property real markIndent: marked.markWidth + Theme.space.sectionGap

        x: marked.markIndent
        width: parent ? parent.width - marked.markIndent : implicitWidth
        showSeparator: false
        valueFont: Theme.font.caption.font

        Rectangle {
            x: -marked.markIndent
            width: marked.markIndent
            height: marked.height
            visible: marked.pressed
            color: Theme.colour.primaryTint
            topLeftRadius: marked.isFirst ? Theme.radius.groupedList : 0
            bottomLeftRadius: marked.isLast ? Theme.radius.groupedList : 0
            Accessible.ignored: true
        }

        Rectangle {
            objectName: "pickerSeparator"
            x: -marked.markIndent
            y: marked.height - height
            width: marked.width + marked.markIndent
            height: Theme.border.hairline
            visible: !marked.isLast
            color: Theme.colour.divider
            Accessible.ignored: true
        }
    }

    // One error type. The swatch is the colour of its tile when it is on the board, and neutral with an outline when it is
    // not. The rows of "Not on your board" are 52 pt and 16 pt text; those of "On your board" are 46 pt and 15 pt.
    component TypeRow: MarkedRow {
        id: typeRow

        required property string typeId
        required property string label
        required property int count
        required property int colourSlot
        property bool onTheBoard: false

        objectName: "pickerRow:" + typeId
        text: typeRow.label
        value: typeRow.count > 0 ? qsTr("%n today", "", typeRow.count) : ""
        minimumHeight: typeRow.onTheBoard ? Theme.size.rowHeightPicker : Theme.size.rowHeightForm
        labelFont: typeRow.onTheBoard ? Theme.font.rowText.font : Theme.font.body.font

        Rectangle {
            objectName: "pickerSwatch"
            x: Theme.space.rowPadding - typeRow.markIndent
            y: (typeRow.height - height) / 2
            width: typeRow.markWidth
            height: width
            radius: Theme.space.tileGap / 2
            color: typeRow.onTheBoard ? Theme.tileColour(typeRow.colourSlot) : Theme.colour.swatch
            border.width: typeRow.onTheBoard ? 0 : Theme.border.hairline
            border.color: Theme.colour.border
            Accessible.ignored: true
        }
    }

    NavBar {
        id: nav

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: qsTr("Log another error")
        inSheet: true
        leadingText: qsTr("Cancel")
        onLeadingTriggered: root.close()
    }

    SearchField {
        id: search

        objectName: "pickerSearch"
        anchors.top: nav.bottom
        // The bar of a sheet leaves 16 pt under its row and the picker's search field has 12.
        anchors.topMargin: -Theme.space.tileGap * 3 / 4
        x: Theme.space.screenMargin
        width: parent.width - 2 * Theme.space.screenMargin
        filter: root.picker.filter
        placeholderText: qsTr("Search error types")
        accessibleName: qsTr("Search error types")
        onFilterEdited: text => root.picker.filter = text
    }

    Flickable {
        id: flick

        objectName: "pickerList"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: search.bottom
        anchors.topMargin: Theme.space.sectionGap
        anchors.bottom: parent.bottom
        clip: true
        contentWidth: width
        contentHeight: sections.height + Theme.space.screenMargin + root.contentBottomInset
        boundsBehavior: Flickable.StopAtBounds
        // While the form is open it is kept in view, as the keyboard shortens the sheet and the message under the field grows.
        onHeightChanged: {
            if (form.open)
                root.showForm();
        }
        onContentHeightChanged: {
            if (form.open)
                root.showForm();
        }

        Column {
            id: sections

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.screenMargin

            GroupedList {
                title: qsTr("Not on your board")
                width: parent.width
                visible: offRepeater.count > 0
                surface: Theme.colour.surfaceRaised
                headingInset: 0

                Repeater {
                    id: offRepeater

                    model: offBoard

                    TypeRow {
                        id: offRow

                        onActivated: root.pick(offRow.typeId)
                    }
                }
            }

            GroupedList {
                title: qsTr("On your board")
                width: parent.width
                visible: onRepeater.count > 0
                surface: Theme.colour.surfaceRaised
                headingInset: 0

                Repeater {
                    id: onRepeater

                    model: onBoard

                    TypeRow {
                        id: onRow

                        onTheBoard: true
                        onActivated: root.pick(onRow.typeId)
                    }
                }
            }

            Text {
                objectName: "pickerEmpty"
                width: parent.width
                visible: offRepeater.count + onRepeater.count === 0
                horizontalAlignment: Text.AlignHCenter
                color: Theme.colour.textSecondary
                font: Theme.font.body.font
                wrapMode: Text.WordWrap
                text: root.picker.filter.length > 0 ? qsTr("No error types match “%1”.").arg(root.picker.filter) : qsTr("There are no error types to choose from.")
                Accessible.role: Accessible.StaticText
                Accessible.name: text
            }

            GroupedList {
                width: parent.width
                surface: Theme.colour.surfaceRaised

                MarkedRow {
                    id: addRow

                    objectName: "pickerAdd"
                    text: qsTr("Add a new error type")
                    markWidth: Theme.size.badge * 5 / 6
                    minimumHeight: Theme.size.rowHeightForm
                    labelColour: Theme.colour.primaryText
                    labelFont: Theme.font.body.make(Font.Medium, false)
                    onActivated: {
                        form.open = true;
                        newName.focusField();
                    }

                    // The plus sign, in the colour of an action.
                    Icon {
                        x: Theme.space.rowPadding - addRow.markIndent
                        y: (addRow.height - height) / 2
                        size: addRow.markWidth
                        kind: "plus"
                        colour: Theme.colour.primaryText
                    }
                }
            }

            // The name of the new type and the button that adds it.
            Item {
                id: addForm

                width: parent.width
                height: Math.max(newName.implicitHeight, addButton.implicitHeight)
                visible: form.open

                FormField {
                    id: newName

                    objectName: "pickerNewName"
                    anchors.left: parent.left
                    anchors.right: addButton.left
                    anchors.rightMargin: Theme.space.tileGap
                    label: qsTr("New error type")
                    labelVisible: false
                    compact: true
                    surface: Theme.colour.surfaceRaised
                    placeholderText: qsTr("Name of the new error type")
                    text: form.name
                    errorText: form.refused ? root.picker.addTypeError : ""
                    onTextEdited: text => {
                        form.name = text;
                        form.refused = false;
                    }
                    onAccepted: root.addType()
                }

                PrimaryButton {
                    id: addButton

                    objectName: "pickerAddButton"
                    anchors.right: parent.right
                    y: Theme.space.tileGap / 2
                    compact: true
                    text: qsTr("Add")
                    accessibleName: qsTr("Add error type")
                    onClicked: root.addType()
                }
            }
        }
    }
}
