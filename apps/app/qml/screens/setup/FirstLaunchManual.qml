// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// First launch, by hand (docs/design/screens/FirstLaunchManual.dc.html, PRF-09): the facilitator and the UNIT prefix, then
// Continue. The two values are given to `setFacilitator` and `setUnitPrefix`. While either is missing the view model
// holds what it has in memory, and when both are set it saves the setup and `configured` becomes true; the shell then
// shows the Log screen. A refusal of either (an empty name, a prefix with a dash) is shown under its field, with a red
// border, and the page stays.
//
// Required: `settings`, the SettingsModel. `backRequested` is "Back"; `completed` is emitted when both values were
// accepted.
EditorPage {
    id: root

    required property var settings

    property string facilitator: root.settings.facilitator
    property string prefix: root.settings.unitPrefix
    property string facilitatorRefusal: ""
    property string prefixRefusal: ""

    // The platform's keyboard, untyped because the QML type system sees it only as a QObject.
    readonly property var inputMethod: Qt.inputMethod
    readonly property real keyboardHeight: inputMethod.visible ? inputMethod.keyboardRectangle.height : 0

    signal backRequested
    signal completed

    function accept(): void {
        root.facilitatorRefusal = root.settings.setFacilitator(root.facilitator);
        root.prefixRefusal = root.settings.setUnitPrefix(root.prefix);
        if (root.facilitatorRefusal.length === 0 && root.prefixRefusal.length === 0)
            root.completed();
    }

    title: qsTr("About you")
    leadingText: qsTr("Back")
    onLeadingTriggered: root.backRequested()

    Flickable {
        id: flick

        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: bottomBlock.top
        clip: true
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            x: Theme.space.screenMargin
            width: flick.width - 2 * Theme.space.screenMargin
            spacing: Theme.space.sectionGap

            Text {
                width: parent.width
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("These head every log you share.")
                wrapMode: Text.WordWrap
            }

            FormField {
                objectName: "facilitatorField"
                label: qsTr("Facilitator")
                text: root.facilitator
                errorText: root.facilitatorRefusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.facilitator = text;
                    root.facilitatorRefusal = "";
                }
            }

            FormField {
                objectName: "prefixField"
                label: qsTr("UNIT prefix")
                text: root.prefix
                placeholderText: qsTr("UNIT")
                hint: qsTr("Added in front of the number you type each day. Letters only, for example UNIT.")
                errorText: root.prefixRefusal
                inputMethodHints: Qt.ImhNoPredictiveText | Qt.ImhNoAutoUppercase
                onTextEdited: text => {
                    root.prefix = text;
                    root.prefixRefusal = "";
                }
                onAccepted: root.accept()
            }
        }
    }

    Column {
        id: bottomBlock

        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.space.screenMargin
        anchors.rightMargin: Theme.space.screenMargin
        anchors.bottomMargin: Theme.space.sectionGap + root.keyboardHeight
        spacing: Theme.space.sectionGap

        PrimaryButton {
            objectName: "continueButton"
            width: parent.width
            text: qsTr("Continue")
            onClicked: root.accept()
        }

        Text {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            color: Theme.colour.textSecondary
            font: Theme.font.caption.font
            text: qsTr("Error types, locations and note templates are added in Settings.")
            wrapMode: Text.WordWrap
        }
    }
}
