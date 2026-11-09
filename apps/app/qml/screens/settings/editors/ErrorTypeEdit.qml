// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The sheet that adds, renames or retires an error type (docs/design/screens/ErrorTypeEdit.dc.html). The Error types
// screen declares one and opens it with `openForNew()` or `openFor(typeId, name, colourSlot)`; the board setup of Settings
// declares one and opens it with `openForPosition(position)` (ErrorTypeNewOnBoard).
//
//   Name     a field; Save asks the view model (`addErrorType` or `renameErrorType`). A refusal (an empty name, a name that
//            is already used by another type, ignoring case, a name over 200 characters, 500 types) is shown under the
//            field, which then has a red border, and Save is dimmed until the person edits the name.
//   Colour   the swatch the type keeps. It is not chosen: the colour follows from the type's place in the master list. A
//            new type shows the colour it will get (`settings.nextColourSlot`) and, under it, where it goes on the board:
//            the position it was opened for (`addErrorTypeAt`), else the first empty one (`addErrorType` puts it there),
//            else that it stays in Other because the board is full.
//   Retire   "Retire this error type" (`retireErrorType`). The type leaves the board and Other and stays on the entries
//            already logged. Not shown for a new type.
//
// The sheet closes itself when the view model accepts the change. The list behind it changes when the save has finished.
Sheet {
    id: root

    required property var settings
    // The type being edited; empty for a new one.
    property string typeId: ""
    property string typeName: ""
    // The colour slot of the type (1 to 11); 0 for a new type, which shows the one it will get.
    property int colourSlot: 0
    // The board position a new type is for (1 to 11), from the board setup; 0 for the first empty one.
    property int boardPosition: 0

    // What the field shows, and why the view model refused it.
    property string draftName: ""
    property string refusal: ""
    property string retireRefusal: ""

    readonly property bool adding: typeId.length === 0
    // The name of the colour the type keeps, such as "Periwinkle".
    readonly property int shownSlot: root.adding ? root.settings.nextColourSlot : root.colourSlot
    readonly property string colourName: shownSlot > 0 ? root.settings.colourName(shownSlot) : ""
    // Where a new type goes on the board: 0 when the board is full.
    readonly property int targetPosition: root.boardPosition > 0 ? root.boardPosition : root.settings.firstEmptyPosition

    function openForNew(): void {
        root.typeId = "";
        root.typeName = "";
        root.colourSlot = 0;
        root.boardPosition = 0;
        root.start();
    }

    // A new type for the board position `position` (1 to 11), from the board setup's picker.
    function openForPosition(position: int): void {
        root.typeId = "";
        root.typeName = "";
        root.colourSlot = 0;
        root.boardPosition = position;
        root.start();
    }

    function openFor(id: string, name: string, slot: int): void {
        root.typeId = id;
        root.typeName = name;
        root.colourSlot = slot;
        root.boardPosition = 0;
        root.start();
    }

    function start(): void {
        root.draftName = root.typeName;
        root.refusal = "";
        root.retireRefusal = "";
        root.open();
        nameField.focusField();
    }

    function save(): void {
        if (root.refusal.length > 0)
            return;
        let message = "";
        if (!root.adding)
            message = root.settings.renameErrorType(root.typeId, root.draftName);
        else if (root.boardPosition > 0)
            message = root.settings.addErrorTypeAt(root.draftName, root.boardPosition);
        else
            message = root.settings.addErrorType(root.draftName);
        if (message.length === 0)
            root.close();
        else
            root.refusal = message;
    }

    function retire(): void {
        const message = root.settings.retireErrorType(root.typeId);
        if (message.length === 0)
            root.close();
        else
            root.retireRefusal = message;
    }

    accessibleName: root.adding ? qsTr("New error type") : qsTr("Error type")
    // The mock-up's sheet starts 120 pt from the top of the window, below the large sheet's 54: its height is fixed, and the
    // content area is what is left under the grabber and above the home indicator.
    detent: Sheet.Content
    contentImplicitHeight: root.height - root.sheetTopOffset - root.grabberZone - root.bottomInset
    // The sheet's distance from the top of the window, on the design frame (ErrorTypeEdit.dc.html).
    readonly property real sheetTopOffset: 120 // literal-ok: the mock-up's own sheet height; there is no token for it

    NavBar {
        id: bar

        // The sheet's body starts 21 pt down, which is the mock-up's own 8 pt, grabber and 8 pt before the row, so the bar
        // sits 8 pt higher than the 8 pt that `inSheet` adds above its row.
        y: -Theme.space.tileGap
        width: parent.width
        inSheet: true
        title: root.adding ? qsTr("New error type") : qsTr("Error type")
        leadingText: qsTr("Cancel")
        trailingText: qsTr("Save")
        trailingEnabled: root.refusal.length === 0
        onLeadingTriggered: root.close()
        onTrailingTriggered: root.save()
    }

    Flickable {
        id: flick

        y: bar.y + bar.height
        width: parent.width
        height: parent.height - y
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin
        boundsBehavior: Flickable.StopAtBounds

        // ErrorTypeEdit: 20 pt above the Colour heading and 28 pt above Retire, set item by item.
        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: 0

            Text {
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.sectionLabel.font
                text: qsTr("Name").toLocaleUpperCase()
                Accessible.ignored: true
            }

            FormField {
                id: nameField

                objectName: "nameField"
                compact: true
                labelVisible: false
                surface: Theme.colour.surfaceRaised
                label: qsTr("Name")
                text: root.draftName
                errorText: root.refusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftName = text;
                    root.refusal = "";
                }
                onAccepted: root.save()
            }

            Item {
                // 20 pt under the message of the field in the mock-up (the component puts 8 pt above that message). It
                // needs a width: a Column skips an item of zero width.
                width: parent.width
                height: root.refusal.length > 0 ? Theme.space.sectionGap : Theme.space.sectionGap * 5 / 3
            }

            GroupedList {
                objectName: "colourGroup"
                width: parent.width
                title: qsTr("Colour")
                footer: !root.adding ? "" : root.targetPosition > 0 ? qsTr("It goes on the board at position %1.").arg(root.targetPosition) : qsTr("The board is full, so it goes in Other. Tap a tile in Settings to put it on the board.")
                surface: Theme.colour.surfaceRaised

                Item {
                    id: colourRow

                    objectName: "colourRow"
                    width: parent.width
                    height: Math.max(Theme.size.listRowHeight, colourText.height + Theme.space.sectionGap)
                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Colour %1. It stays with the type when the board is reordered.").arg(root.colourName)

                    Rectangle {
                        objectName: "editSwatch"
                        x: Theme.space.rowPadding
                        y: (parent.height - height) / 2
                        width: Theme.size.badge * 3 / 4
                        height: width
                        radius: Theme.radius.field / 3
                        color: Theme.tileColour(root.shownSlot)
                        border.width: Theme.border.hairline
                        border.color: Theme.colour.border
                        Accessible.ignored: true
                    }

                    Column {
                        id: colourText

                        x: Theme.space.rowPadding + Theme.size.badge * 3 / 4 + Theme.space.tileGap + Theme.border.hairline * 2
                        y: (parent.height - height) / 2
                        width: parent.width - x - Theme.space.rowPadding

                        Text {
                            objectName: "colourName"
                            width: parent.width
                            color: Theme.colour.textPrimary
                            font: Theme.font.body.font
                            text: root.colourName
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }

                        Item {
                            width: parent.width
                            height: Theme.border.hairline * 2
                        }

                        Text {
                            width: parent.width
                            color: Theme.colour.textSecondary
                            font: Theme.font.caption.font
                            text: qsTr("Stays with the type when the board is reordered")
                            wrapMode: Text.WordWrap
                            Accessible.ignored: true
                        }
                    }
                }
            }

            Item {
                width: parent.width
                height: root.adding ? 0 : Theme.space.sectionGap * 7 / 3
            }

            GroupedList {
                width: parent.width
                visible: !root.adding
                surface: Theme.colour.surfaceRaised
                footer: root.retireRefusal.length > 0 ? root.retireRefusal : qsTr("A retired type disappears from the board and from Other. It stays on entries already logged.")

                // The mock-up's button: a 54 pt card with the label centred, 17/600, in the destructive colour.
                Item {
                    id: retireRow

                    objectName: "retireRow"
                    readonly property bool pressed: retireTap.pressed
                    readonly property bool destructive: true

                    signal activated

                    width: parent.width
                    height: Math.max(Theme.size.primaryButtonHeight, retireLabel.implicitHeight + 2 * Theme.space.sectionGap)
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Retire this error type")
                    Accessible.focusable: true
                    Accessible.onPressAction: root.retire()
                    onActivated: root.retire()
                    Keys.onReturnPressed: root.retire()
                    Keys.onSpacePressed: root.retire()

                    Rectangle {
                        anchors.fill: parent
                        visible: retireRow.pressed
                        radius: Theme.radius.groupedList
                        color: Theme.colour.primaryTint
                    }

                    Text {
                        id: retireLabel

                        objectName: "retireLabel"
                        x: Theme.space.rowPadding
                        width: parent.width - 2 * Theme.space.rowPadding
                        y: (parent.height - height) / 2
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.colour.destructive
                        font: Theme.font.sheetTitle.font
                        text: qsTr("Retire this error type")
                        wrapMode: Text.WordWrap
                        Accessible.ignored: true
                    }

                    TapHandler {
                        id: retireTap

                        onTapped: retireRow.activated()
                    }
                }
            }
        }
    }
}
