// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// First launch, by hand (docs/design/screens/FirstLaunchManual.dc.html, PRF-09): the tester and the UNIT prefix, then
// Continue. The two values are given to `setTester` and `setUnitPrefix`. While either is missing the view model
// holds what it has in memory, and when both are set it saves the setup and `configured` becomes true; the shell then
// shows the Log screen. A refusal of either (an empty name, a prefix with a dash) is shown under its field, with a red
// border, and the page stays.
//
// The page ends at the top of the keyboard (EditorPage's frame), so Continue and the line under it sit just above it, and the
// field being typed in is scrolled into view in the page's Flickable.
//
// Required: `settings`, the SettingsModel. `backRequested` is "Back"; `completed` is emitted when both values were
// accepted.
EditorPage {
    id: root

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // form column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

    required property var settings

    property string tester: root.settings.tester
    property string prefix: root.settings.unitPrefix
    property string testerRefusal: ""
    property string prefixRefusal: ""

    signal backRequested
    signal completed

    function accept(): void {
        root.testerRefusal = root.settings.setTester(root.tester);
        root.prefixRefusal = root.settings.setUnitPrefix(root.prefix);
        if (root.testerRefusal.length === 0 && root.prefixRefusal.length === 0)
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

            x: root.side
            width: flick.width - 2 * root.side
            // 20 pt between the parts, as the design has them (a 12 pt margin and the 8 pt gap).
            spacing: Theme.space.sectionGap + Theme.space.tileGap

            Text {
                width: parent.width
                // The first label has 4 pt more above it than the others.
                bottomPadding: Theme.space.tileGap / 2
                color: Theme.colour.textSecondary
                font: Theme.font.bodySmall.font
                text: qsTr("These head every log you share.")
                wrapMode: Text.WordWrap
            }

            FormField {
                objectName: "testerField"
                label: qsTr("Tester")
                compact: true
                border: true
                text: root.tester
                errorText: root.testerRefusal
                inputMethodHints: Qt.ImhNoPredictiveText
                onTextEdited: text => {
                    root.tester = text;
                    root.testerRefusal = "";
                }
            }

            FormField {
                objectName: "prefixField"
                label: qsTr("Unit prefix")
                compact: true
                border: true
                text: root.prefix
                placeholderText: qsTr("For example UNIT")
                hint: root.settings.unitPrefixHint
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
        anchors.leftMargin: root.side
        anchors.rightMargin: root.side
        // The button and the line under it are 10 pt apart, and the line ends at the bottom of the safe area.
        spacing: Theme.space.tileGap + Theme.border.hairline * 2

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
