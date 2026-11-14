// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The sheet that adds, edits or retires an interaction type (Stage 14, 4.5; InteractionTypeEdit). The Interaction types
// screen declares one and opens it with `openForNew()` or `openFor(typeId, name, line)`.
//
//   Name          a field. Save asks the view model (`addInteractionType` or `updateInteractionType`); a refusal is shown
//                 under the field.
//   Line in the log   optional. [person] is where the people go, [answer] is where an answer typed after the tap goes. A
//                 type with [person] asks who, one with [answer] asks a question, and one with neither logs at once. With
//                 no line the log shows the name. A type with no [person] never opens the picker.
//   Retire        "Retire this interaction type": it leaves the board and Other and stays on the entries already logged.
//
// The sheet closes itself when the view model accepts the change.
Sheet {
    id: root

    required property var settings
    property string typeId: ""
    property string typeName: ""
    property string typeLine: ""

    property string draftName: ""
    property string draftLine: ""
    property string refusal: ""
    property string retireRefusal: ""

    readonly property bool adding: typeId.length === 0
    readonly property string personToken: "[person]"
    readonly property string answerToken: "[answer]"
    // The words of the line problem, shown under the field while the person types; empty for a line that can be saved.
    readonly property string lineMessage: root.settings.validateInteractionLine(root.draftLine)
    readonly property string previewText: root.settings.previewInteractionLine(root.draftName, root.draftLine)
    readonly property string asksText: {
        if (root.lineMessage.length > 0)
            return "";
        if (root.draftLine.indexOf(root.personToken) >= 0)
            return qsTr("After you log it, you pick the people.");
        if (root.draftLine.indexOf(root.answerToken) >= 0)
            return qsTr("After you log it, you are asked for an answer.");
        return qsTr("It is logged at once and asks nothing.");
    }

    function openForNew(): void {
        root.typeId = "";
        root.typeName = "";
        root.typeLine = "";
        root.start();
    }

    function openFor(id: string, name: string, line: string): void {
        root.typeId = id;
        root.typeName = name;
        root.typeLine = line;
        root.start();
    }

    function start(): void {
        root.draftName = root.typeName;
        root.draftLine = root.typeLine;
        root.refusal = "";
        root.retireRefusal = "";
        root.open();
        nameField.focusField();
    }

    function save(): void {
        if (root.refusal.length > 0 || root.lineMessage.length > 0)
            return;
        const message = root.adding ? root.settings.addInteractionType(root.draftName, root.draftLine) : root.settings.updateInteractionType(root.typeId, root.draftName, root.draftLine);
        if (message.length === 0)
            root.close();
        else
            root.refusal = message;
    }

    function retire(): void {
        const message = root.settings.retireInteractionType(root.typeId);
        if (message.length === 0)
            root.close();
        else
            root.retireRefusal = message;
    }

    accessibleName: root.adding ? qsTr("New interaction type") : qsTr("Interaction type")
    detent: Sheet.Content
    contentImplicitHeight: root.height - root.sheetTopOffset - root.grabberZone - root.bottomInset
    readonly property real sheetTopOffset: 120 // literal-ok: as ErrorTypeEdit, which this sheet follows

    NavBar {
        id: bar

        y: -Theme.space.tileGap
        width: parent.width
        inSheet: true
        title: root.adding ? qsTr("New interaction type") : qsTr("Interaction type")
        leadingText: qsTr("Cancel")
        trailingText: qsTr("Save")
        trailingEnabled: root.refusal.length === 0 && root.lineMessage.length === 0
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

        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: 0

            FormField {
                id: nameField

                objectName: "nameField"
                compact: true
                border: true
                label: qsTr("Name")
                text: root.draftName
                hint: qsTr("This is what the tile says.")
                errorText: root.refusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftName = text;
                    root.refusal = "";
                }
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap + Theme.space.tileGap
            }

            FormField {
                id: lineField

                objectName: "lineField"
                compact: true
                border: true
                label: qsTr("Line in the log")
                text: root.draftLine
                hint: qsTr("Optional. Leave it empty to log the name. Tap Insert person to put %1 where the people go.").arg(root.personToken)
                errorText: root.lineMessage
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.draftLine = text;
                    root.refusal = "";
                }
            }

            Item {
                width: parent.width
                height: Theme.space.tileGap
            }

            Row {
                spacing: Theme.space.tileGap

                PrimaryButton {
                    objectName: "insertPersonButton"
                    variant: PrimaryButton.Secondary
                    compact: true
                    iconKind: "plus"
                    text: qsTr("Insert person")
                    accessibleName: qsTr("Insert person")
                    Accessible.description: qsTr("Puts %1 in the line at the cursor. The people you pick take its place.").arg(root.personToken)
                    onClicked: lineField.insertAtCursor(root.personToken)
                }

                PrimaryButton {
                    objectName: "insertAnswerButton"
                    variant: PrimaryButton.Secondary
                    compact: true
                    iconKind: "plus"
                    text: qsTr("Insert answer")
                    accessibleName: qsTr("Insert answer")
                    Accessible.description: qsTr("Puts %1 in the line at the cursor. The answer you type takes its place.").arg(root.answerToken)
                    onClicked: lineField.insertAtCursor(root.answerToken)
                }
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap * 3 / 2
            }

            Text {
                objectName: "asksText"
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.description.font
                text: root.asksText
                wrapMode: Text.WordWrap
                visible: text.length > 0
            }

            Item {
                width: parent.width
                height: Theme.space.sectionGap
            }

            Text {
                x: Theme.space.tileGap / 2
                width: parent.width - 2 * x
                color: Theme.colour.textSecondary
                font: Theme.font.sectionLabel.font
                text: qsTr("Example").toLocaleUpperCase()
                Accessible.role: Accessible.Heading
                Accessible.name: qsTr("Example")
            }

            Item {
                width: parent.width
                height: Theme.space.tileGap
            }

            Rectangle {
                objectName: "exampleCard"
                width: parent.width
                height: previewLabel.height + 2 * Theme.space.rowPadding
                radius: Theme.radius.groupedList
                color: Theme.colour.primaryTint
                border.width: Theme.border.hairline
                border.color: Theme.colour.border
                Accessible.role: Accessible.StaticText
                Accessible.name: previewLabel.text

                Text {
                    id: previewLabel

                    objectName: "previewText"
                    x: Theme.space.rowPadding
                    y: Theme.space.rowPadding
                    width: parent.width - 2 * Theme.space.rowPadding
                    color: root.lineMessage.length > 0 ? Theme.colour.textSecondary : Theme.colour.textPrimary
                    font: Theme.font.body.font
                    text: root.previewText
                    wrapMode: Text.WordWrap
                    Accessible.ignored: true
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
                    Accessible.name: qsTr("Retire this interaction type")
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
                        text: qsTr("Retire this interaction type")
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
