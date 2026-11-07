// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The first-launch screen (docs/design/screens/FirstLaunch.dc.html, PRF-09), shown while `settings.configured` is false and
// the settings have finished loading. It names the app, CS Bingo Board, and offers two plain choices:
//
//   Import a setup file    emits `importRequested()`. The shell opens the platform's file dialog and, with the chosen file,
//                          calls ImportFlow.begin(); that shows the confirmation or the problems with the file.
//   Set up by hand         opens FirstLaunchManual, which asks for the facilitator and the UNIT prefix. The view model
//                          holds them in memory until both are set, then saves and `configured` becomes true.
//
// When the saved settings could not be used at launch (`settings.loadProblem`: an app that is too old for the file, a
// damaged file) the reason is a banner above the choices; the file is kept.
//
// Required: `settings`, the SettingsModel. `completed()` is emitted when the manual setup was accepted; the shell hides
// this screen when `configured` is true.
Item {
    id: root

    required property var settings

    // "", "import" or "manual": the card the person chose last.
    property string choice: ""
    // The banner for a problem with the saved settings was closed.
    property bool problemDismissed: false

    signal importRequested
    signal completed

    PageStack {
        id: stack

        anchors.fill: parent
        accessibleName: qsTr("CS Bingo Board")
        initialItem: welcomePage
    }

    Component {
        id: welcomePage

        Rectangle {
            color: Theme.colour.background

            SafeFrame {
                anchors.fill: parent

                Flickable {
                    id: flick

                    anchors.fill: parent
                    clip: true
                    contentWidth: width
                    contentHeight: content.height
                    boundsBehavior: Flickable.StopAtBounds

                    Item {
                        id: content

                        width: flick.width
                        height: Math.max(flick.height, intro.height + cards.height + 3 * Theme.space.screenMargin)

                        Column {
                            id: intro

                            x: Theme.space.screenMargin
                            width: parent.width - 2 * Theme.space.screenMargin
                            spacing: Theme.space.sectionGap

                            Banner {
                                objectName: "loadProblemBanner"
                                width: parent.width
                                visible: root.settings.loadProblem.length > 0 && !root.problemDismissed
                                tone: Banner.Warning
                                title: qsTr("Your saved settings were not used")
                                message: root.settings.loadProblem
                                onCloseRequested: root.problemDismissed = true
                            }

                            Rectangle {
                                objectName: "appIcon"
                                anchors.horizontalCenter: parent.horizontalCenter
                                width: Theme.size.tileMinimum
                                height: width
                                radius: Theme.radius.headerCard + Theme.space.tileGap / 2
                                color: Theme.colour.headerCard
                                Accessible.ignored: true

                                Grid {
                                    anchors.centerIn: parent
                                    columns: 3
                                    spacing: Theme.border.hairline * 3

                                    Repeater {
                                        model: 9

                                        delegate: Rectangle {
                                            required property int index

                                            width: Theme.size.badge * 0.9
                                            height: width
                                            radius: Theme.border.hairline * 3
                                            color: Theme.tileColour(index + 1)
                                        }
                                    }
                                }
                            }

                            Text {
                                objectName: "appTitle"
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                color: Theme.colour.textPrimary
                                font: Theme.font.largeTitle.font
                                text: qsTr("CS Bingo Board")
                                wrapMode: Text.WordWrap
                                Accessible.role: Accessible.Heading
                                Accessible.name: text
                            }

                            Text {
                                width: parent.width
                                horizontalAlignment: Text.AlignHCenter
                                color: Theme.colour.textSecondary
                                font: Theme.font.body.font
                                text: qsTr("Log what goes wrong during a session, as it happens, and share the log at the end of the day.")
                                wrapMode: Text.WordWrap
                            }
                        }

                        Column {
                            id: cards

                            x: Theme.space.screenMargin
                            y: parent.height - height - Theme.space.screenMargin
                            width: parent.width - 2 * Theme.space.screenMargin
                            spacing: Theme.space.sectionGap

                            Text {
                                x: Theme.space.tileGap / 2
                                width: parent.width - 2 * x
                                color: Theme.colour.textSecondary
                                font: Theme.font.sectionLabel.font
                                text: qsTr("How do you want to start?").toLocaleUpperCase()
                                wrapMode: Text.WordWrap
                                Accessible.role: Accessible.Heading
                                Accessible.name: qsTr("How do you want to start?")
                            }

                            ChoiceCard {
                                objectName: "importCard"
                                title: qsTr("Import a setup file")
                                description: qsTr("A setup file holds your error types, locations and note templates. You can import one later in Settings.")
                                selected: root.choice === "import"
                                onClicked: {
                                    root.choice = "import";
                                    root.importRequested();
                                }
                            }

                            ChoiceCard {
                                objectName: "manualCard"
                                title: qsTr("Set up by hand")
                                description: qsTr("Enter your name and the UNIT prefix. Error types, locations and note templates are added in Settings.")
                                selected: root.choice === "manual"
                                onClicked: {
                                    root.choice = "manual";
                                    stack.push(manualPage);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: manualPage

        FirstLaunchManual {
            settings: root.settings
            onBackRequested: {
                root.choice = "";
                stack.pop();
            }
            onCompleted: root.completed()
        }
    }
}
