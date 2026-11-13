// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The Environment sheet of Today's details (PRF-11; design `Header.dc.html`, "Environment" row): the short list that the
// Environment row opens. "None" comes first, then the environments of the setup in the order it lists them (the default is
// "prod" and "dev"), and the one in force is ticked. The environment is picked and never typed, so there is no text field.
//
// A choice closes the sheet and is reported by `chosen`, with "" for None; the screen decides what to do with it (the unit
// is saved with its number). Cancel, the grabber and Escape leave everything as it was. A log may carry an environment that
// a later setup file no longer lists; it is shown as the last choice, ticked, so the sheet never hides what the log has.
//
// It is as tall as its list (Sheet.Content), and scrolls when a long list does not fit.
Sheet {
    id: root

    required property var settings

    // The environment in force, "" for none.
    property string current: ""

    // Every choice in the order shown: "" is None.
    readonly property var choices: {
        const names = [""];
        const listed = root.settings.unitEnvironments;
        for (let i = 0; i < listed.length; ++i)
            names.push(listed[i]);
        if (current.length > 0 && names.indexOf(current) < 0)
            names.push(current);
        return names;
    }

    // An environment was picked: its name, or "" for None.
    signal chosen(string environment)

    // Presents the sheet with `environment` ticked.
    function show(environment: string): void {
        current = environment;
        open();
    }

    accessibleName: qsTr("Number suffix")
    detent: Sheet.Content
    contentImplicitHeight: bar.height + list.height + Theme.space.screenMargin

    Item {
        anchors.fill: parent

        NavBar {
            id: bar

            objectName: "environmentBar"
            width: parent.width
            inSheet: true
            title: qsTr("Number suffix")
            leadingText: qsTr("Cancel")
            onLeadingTriggered: root.close()
        }

        Flickable {
            id: flick

            objectName: "environmentFlickable"
            y: bar.height
            width: parent.width
            height: parent.height - y
            contentWidth: width
            contentHeight: list.height + Theme.space.screenMargin
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            GroupedList {
                id: list

                objectName: "environmentList"
                x: Theme.space.screenMargin
                width: flick.width - 2 * x
                surface: Theme.colour.surfaceRaised

                Repeater {
                    model: root.choices

                    delegate: ListRow {
                        id: row

                        required property string modelData

                        objectName: row.modelData.length > 0 ? "environment_" + row.modelData : "environmentNone"
                        text: row.modelData.length > 0 ? row.modelData : qsTr("None")
                        checkable: true
                        checked: row.modelData === root.current
                        onActivated: {
                            root.chosen(row.modelData);
                            root.close();
                        }
                    }
                }
            }
        }
    }
}
