// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The note card of the Log screen: a row of template chips over a text field with an "Add" button (NOTE-01 to 05).
//
// `draft` is the note being typed, `session.draft`. The field shows it and every change the user makes is announced with
// `textEdited(text)`; the screen stores it (the 300 ms debounce is in the session). The field never assigns `draft`, so a
// binding on it stays alive, and a text that arrives from outside (the note that a note undo gives back, or an empty
// text after the note was saved) replaces the text in the field with the cursor at its end. The same text arriving as the
// echo of the user's own typing changes nothing, so the cursor stays where it is.
//
// The field is multi-line: Return starts a new line, and Add emits `submit(text)` when the text is not empty or only spaces;
// otherwise it does nothing. At rest the field is one line high; with focus it opens over what is under the card (see `box`). The composer does not clear itself: the
// screen calls the session, the session clears the draft when the note is committed, and the field follows `draft`. A note
// that was refused is therefore still in the field.
//
// `chips` is a model with the roles `templateId` and `name` (`settings.templates`). A chip emits `chipChosen(templateId)`.
// With `skeleton` the card draws its shapes only, in neutral colours and with no text, for the launch screen (Stage 5).
//
// `chipIcons` maps a templateId to an Icon kind for its chip ({"t1": "music"}); a chip without one has no icon. Look
// (Main): 12 pt padding, 8 pt between chips, 10 pt between the chips and the field, the card shadow.
Item {
    id: root

    required property string draft
    required property var chips
    property bool skeleton: false
    // With `skeleton`: the Add button's shape is the least width of the button (64 pt, which "Add" fills on the phone) and not the
    // width of its label in the font of the machine, so that the launch image comes out the same on every machine.
    property bool nominalMetrics: false
    property string placeholderText: qsTr("Type a note, then hold a tile…")
    property string fieldName: qsTr("Journal note")
    property string addText: qsTr("Add")
    // An Icon kind for each template's chip, by templateId.
    property var chipIcons: ({})
    // The most lines the open box shows before it scrolls.
    property int maxLines: 5

    readonly property bool canSubmit: field.text.trim().length > 0
    // The width of the Add button, or of the shape in its place.
    readonly property real buttonWidth: skeleton && nominalMetrics ? Theme.size.tileMinimum * 8 / 11 : addButton.implicitWidth

    // The user changed the text.
    signal textEdited(string text)
    // Add, with text in the field.
    signal submit(string text)
    // A template chip was tapped.
    signal chipChosen(string templateId)

    function focusField(): void {
        field.forceActiveFocus();
    }

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: column.height + 2 * Theme.space.sectionGap
    // The open box overflows the card, over the board under it, so the card stands above its neighbours while it is open.
    z: field.activeFocus ? 1 : 0
    width: parent ? parent.width : implicitWidth

    onDraftChanged: {
        if (field.text !== draft) {
            field.text = draft;
            field.cursorPosition = field.length;
        }
    }
    Component.onCompleted: field.text = draft

    Rectangle {
        objectName: "noteShadow"
        visible: !root.skeleton
        width: parent.width
        height: parent.height
        y: Theme.shadow.card.offsetY
        radius: Theme.radius.card
        color: Theme.shadow.card.colour
    }

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.card
        color: root.skeleton ? Theme.colour.swatch : Theme.colour.surface
    }

    Column {
        id: column

        x: Theme.space.sectionGap
        y: Theme.space.sectionGap
        width: parent.width - 2 * Theme.space.sectionGap
        spacing: Theme.space.tileGap + Theme.border.hairline * 2

        // The chips wrap onto more rows when there is no room (a long name). From a text scale of 1.2 up they are one row that
        // scrolls sideways, so that the board keeps its height (Stage 7, F1).
        Flickable {
            id: chipScroll

            readonly property bool oneRow: Theme.clampedScale > 1.2

            width: parent.width
            height: chipRow.height
            clip: oneRow
            contentWidth: oneRow ? chipRow.childrenRect.width : width
            contentHeight: chipRow.height
            flickableDirection: Flickable.HorizontalFlick
            interactive: oneRow && contentWidth > width
            boundsBehavior: Flickable.StopAtBounds
            visible: root.skeleton || chipRepeater.count > 0

            Flow {
                id: chipRow

                width: chipScroll.oneRow ? 100000 : chipScroll.width // literal-ok: wider than any row of chips, so that the chips do not wrap
                spacing: Theme.space.tileGap

                Repeater {
                    id: chipRepeater

                    model: root.skeleton ? 0 : root.chips

                    delegate: Chip {
                        required property string templateId
                        required property string name

                        objectName: "chip"
                        text: name
                        iconKind: root.chipIcons[templateId] !== undefined ? root.chipIcons[templateId] : ""
                        maxWidth: chipScroll.oneRow ? 0 : chipRow.width
                        onClicked: root.chipChosen(templateId)
                    }
                }

                // The launch screen's shapes: two chips.
                Repeater {
                    model: root.skeleton ? 2 : 0

                    delegate: Rectangle {
                        width: Theme.size.minTouchTarget * 2
                        height: Theme.size.chipHeight
                        radius: Theme.radius.chip
                        color: Theme.colour.border
                    }
                }
            }
        }

        Row {
            id: fieldRow

            width: parent.width
            spacing: Theme.space.tileGap
            // The row is as high as the one-line box, whatever the box is doing, so that what is under the card never moves.
            height: Theme.size.fieldHeight
            z: field.activeFocus ? 1 : 0

            // The box. At rest it is one line high and shows the first line of the note. With focus it opens downwards over
            // what is under the card, to the height of the text and at most `maxLines` lines (and half the window), and
            // scrolls inside beyond that; the keyboard covers the board by then. It folds back when focus leaves.
            Rectangle {
                id: box

                objectName: "noteBox"

                readonly property real linePitch: Theme.font.bodySmall.linePitch
                readonly property real openLimit: Math.min(Theme.size.fieldHeight + (root.maxLines - 1) * linePitch, root.Window.height > 0 ? root.Window.height / 2 : Number.MAX_VALUE)

                width: parent.width - root.buttonWidth - parent.spacing
                height: field.activeFocus ? Math.max(Theme.size.fieldHeight, Math.min(field.implicitHeight, openLimit)) : Theme.size.fieldHeight
                radius: Theme.radius.field
                color: root.skeleton ? Theme.colour.border : Theme.colour.fieldBackground
                border.width: field.activeFocus ? Theme.border.otherWidth : Theme.border.hairline
                border.color: field.activeFocus ? Theme.colour.primaryText : Theme.colour.border

                Flickable {
                    id: flick

                    anchors.fill: parent
                    anchors.margins: box.border.width
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: field.activeFocus && contentHeight > height

                    TextArea.flickable: TextArea {
                        id: field

                        objectName: "noteInput"
                        leftPadding: Theme.space.sectionGap
                        rightPadding: Theme.space.sectionGap
                        topPadding: Theme.space.tileGap
                        bottomPadding: Theme.space.tileGap
                        // At least the one-line box high (a touch target), and as high as the text beyond that.
                        implicitHeight: Math.max(Theme.size.fieldHeight, contentHeight + topPadding + bottomPadding)
                        height: Math.max(implicitHeight, flick.height)
                        verticalAlignment: TextEdit.AlignVCenter
                        color: Theme.colour.textPrimary
                        font: Theme.font.bodySmall.font
                        wrapMode: TextEdit.Wrap
                        placeholderText: root.skeleton ? "" : root.placeholderText
                        placeholderTextColor: Theme.colour.textPlaceholder
                        selectionColor: Theme.colour.primaryTint
                        selectedTextColor: Theme.colour.textPrimary
                        enabled: !root.skeleton
                        background: null
                        EnterKey.type: Qt.EnterKeyReturn

                        Accessible.ignored: root.skeleton
                        Accessible.role: Accessible.EditableText
                        Accessible.name: root.fieldName

                        // A change that is not the draft coming back is the user's.
                        onTextChanged: {
                            if (text !== root.draft)
                                root.textEdited(text);
                        }
                        onActiveFocusChanged: {
                            if (!activeFocus)
                                flick.contentY = 0;
                        }
                    }
                }

                // Shows that there is more text than the box holds. It is a drawing: it takes no touch and has no name.
                Rectangle {
                    readonly property real room: box.height - 4 * Theme.border.hairline

                    visible: field.activeFocus && flick.contentHeight > flick.height
                    x: box.width - width - 2 * Theme.border.hairline
                    y: 2 * Theme.border.hairline + flick.visibleArea.yPosition * room
                    width: Theme.border.otherWidth
                    height: flick.visibleArea.heightRatio * room
                    radius: width / 2
                    color: Theme.colour.borderStrong
                }
            }

            PrimaryButton {
                id: addButton

                objectName: "addButton"
                height: Theme.size.fieldHeight
                compact: true
                text: root.addText
                visible: !root.skeleton
                onClicked: {
                    if (root.canSubmit)
                        root.submit(field.text);
                }
            }

            // The launch screen's button shape.
            Rectangle {
                visible: root.skeleton
                width: root.buttonWidth
                height: Theme.size.fieldHeight
                radius: Theme.radius.field
                color: Theme.colour.border
            }
        }
    }
}
