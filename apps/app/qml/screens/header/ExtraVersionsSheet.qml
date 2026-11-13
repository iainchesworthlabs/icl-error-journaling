// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The Other versions sheet of Today's details (HDR-10): the software versions besides the robot's, each with a name, for
// example "Influencer" (the phone app that controls the robot) and 4.5.0. The Header shows them as one row ("Other
// versions", with "Influencer v4.5.0" as its value) so that the screen does not grow with every version; this sheet is
// where they are added, edited and removed.
//
// Each version is a card of a name field, a version field with a fixed "v" before it (nobody types it; one that is typed is
// accepted and not doubled) and a Remove row. "Add a version" adds an empty card. Every row is checked as it is typed with
// `session.validateExtraRow`, which judges the name, the version and the name against the other names, and
// `session.extraRowField` says which of the two fields the message belongs to. Done saves the list as a Correct
// (`session.correctExtraVersions`) and closes; it stays disabled while a row is refused. A card with nothing in it is
// dropped on Done. Cancel, the grabber and Escape leave everything as it was.
//
// A version that changed during the day is edited here as a correction; the log gets no line for it.
Sheet {
    id: root

    required property var session

    // The rows being edited: {label, version, commit}. They are changed in place, and `revision` says that they were, so a
    // field that is being typed in is not rebuilt.
    property var rows: []
    property int revision: 0

    readonly property bool refused: {
        // Reads the counter so that the binding follows it (a plain statement would be linted).
        if (root.revision < 0)
            return false;
        for (let i = 0; i < root.rows.length; ++i) {
            if (root.session.validateExtraRow(root.rows, i).length > 0)
                return true;
        }
        return false;
    }

    // Presents the sheet with the versions in force.
    function show(): void {
        const current = session.extraVersions;
        const copy = [];
        for (let i = 0; i < current.length; ++i)
            copy.push({
                "label": current[i].label,
                "version": current[i].version,
                "commit": current[i].commit
            });
        rows = copy;
        revision++;
        open();
    }

    function addRow(): void {
        const next = rows.slice();
        next.push({
            "label": "",
            "version": "",
            "commit": ""
        });
        rows = next;
        revision++;
        Qt.callLater(() => {
            const last = rowRepeater.itemAt(rows.length - 1);
            if (last !== null)
                last.focusLabel();
        });
    }

    function removeRow(index: int): void {
        const next = rows.slice();
        next.splice(index, 1);
        rows = next;
        revision++;
    }

    function save(): void {
        if (refused)
            return;
        if (session.correctExtraVersions(rows))
            close();
    }

    accessibleName: qsTr("Other versions")
    detent: Sheet.Large

    Item {
        anchors.fill: parent

        NavBar {
            id: bar

            objectName: "extraVersionsBar"
            width: parent.width
            inSheet: true
            title: qsTr("Other versions")
            leadingText: qsTr("Cancel")
            trailingText: qsTr("Done")
            trailingEnabled: !root.refused
            onLeadingTriggered: root.close()
            onTrailingTriggered: root.save()
        }

        Flickable {
            id: flick

            objectName: "extraVersionsFlickable"
            y: bar.height
            width: parent.width
            height: parent.height - y
            contentWidth: width
            contentHeight: column.height + Theme.space.screenMargin * 2
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Column {
                id: column

                x: Theme.space.screenMargin
                width: flick.width - 2 * x
                spacing: Theme.space.sectionGap

                Text {
                    objectName: "extraVersionsIntro"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.colour.textSecondary
                    font: Theme.font.caption.font
                    text: qsTr("Other software that is running, such as the phone app that controls the robot. The robot’s own version is on Today’s details.")
                }

                Repeater {
                    id: rowRepeater

                    model: root.rows.length

                    delegate: Column {
                        id: card

                        required property int index

                        readonly property var row: {
                            if (root.revision < 0)
                                return null;
                            return root.rows[card.index] ?? {
                                "label": "",
                                "version": "",
                                "commit": ""
                            };
                        }
                        readonly property string problem: {
                            if (root.revision < 0)
                                return "";
                            return root.session.validateExtraRow(root.rows, card.index);
                        }
                        readonly property string problemField: {
                            if (root.revision < 0)
                                return "";
                            return root.session.extraRowField(root.rows, card.index);
                        }

                        function focusLabel(): void {
                            labelField.focusField();
                        }

                        objectName: "extraVersionCard"
                        width: column.width
                        spacing: Theme.space.tileGap

                        FormField {
                            id: labelField

                            objectName: "extraVersionLabel"
                            width: parent.width
                            label: qsTr("Name")
                            text: card.row.label
                            placeholderText: qsTr("For example Influencer")
                            inputMethodHints: Qt.ImhNoPredictiveText
                            errorText: card.problemField === "label" ? card.problem : ""
                            onTextEdited: text => {
                                root.rows[card.index].label = text;
                                root.revision++;
                            }
                        }

                        FormField {
                            objectName: "extraVersionNumber"
                            width: parent.width
                            label: qsTr("Version")
                            prefixText: "v"
                            text: card.row.version
                            placeholderText: qsTr("For example 4.5.0")
                            hint: qsTr("Numbers only, like 4.5.0. The v is added for you.")
                            inputMethodHints: Qt.ImhFormattedNumbersOnly
                            errorText: card.problemField === "version" ? card.problem : ""
                            onTextEdited: text => {
                                root.rows[card.index].version = text;
                                root.revision++;
                            }
                        }

                        GroupedList {
                            width: parent.width

                            ListRow {
                                objectName: "extraVersionRemove"
                                text: qsTr("Remove this version")
                                destructive: true
                                onActivated: root.removeRow(card.index)
                            }
                        }
                    }
                }

                PrimaryButton {
                    objectName: "extraVersionAdd"
                    width: parent.width
                    height: Theme.size.smallButtonHeight
                    text: qsTr("Add a version")
                    onClicked: root.addRow()
                }
            }
        }
    }
}
