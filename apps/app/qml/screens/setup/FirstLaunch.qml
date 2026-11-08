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

                        // The hero is 70 pt under the safe area, as drawn, or 12 pt under the banner of a saved file that
                        // could not be used.
                        readonly property real heroOffset: Theme.size.primaryButtonHeight + Theme.space.screenMargin

                        width: flick.width
                        height: Math.max(flick.height, intro.height + choices.height + Theme.space.screenMargin)

                        Column {
                            id: intro

                            x: Theme.space.screenMargin
                            width: parent.width - 2 * Theme.space.screenMargin
                            spacing: Theme.space.sectionGap

                            Banner {
                                id: problemBanner

                                objectName: "loadProblemBanner"
                                width: parent.width
                                visible: root.settings.loadProblem.length > 0 && !root.problemDismissed
                                tone: Banner.Warning
                                title: qsTr("Your saved settings were not used")
                                message: root.settings.loadProblem
                                onCloseRequested: root.problemDismissed = true
                            }

                            Item {
                                width: parent.width
                                height: problemBanner.visible ? 0 : content.heroOffset - Theme.space.sectionGap
                            }

                            // The icon, the name and the line under it: 14 pt apart, with 12 pt either side of the text.
                            Column {
                                id: hero

                                x: Theme.space.sectionGap
                                width: parent.width - 2 * x
                                spacing: Theme.space.rowPadding

                                Rectangle {
                                    objectName: "appIcon"
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: Theme.size.tileMinimum - Theme.space.tileGap / 2
                                    height: width
                                    radius: Theme.size.badge
                                    color: Theme.colour.headerCard
                                    Accessible.ignored: true

                                    Icon {
                                        objectName: "appIconGlyph"
                                        anchors.centerIn: parent
                                        kind: "list"
                                        size: Theme.size.badge * 5 / 3
                                        colour: Theme.colour.textOnHeaderCard
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
                                    objectName: "appSummary"
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    color: Theme.colour.textSecondary
                                    font: Theme.font.body.font
                                    lineHeightMode: Text.FixedHeight
                                    lineHeight: Theme.font.body.size * 1.45
                                    text: qsTr("Log what goes wrong during a session, as it happens, and share the log at the end of the day.")
                                    wrapMode: Text.WordWrap
                                }
                            }
                        }

                        // The two ways to start, at the foot, 10 pt apart.
                        Column {
                            id: choices

                            x: Theme.space.screenMargin
                            y: parent.height - height
                            width: parent.width - 2 * Theme.space.screenMargin
                            spacing: Theme.space.tileGap + Theme.border.hairline * 2

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

                            PrimaryButton {
                                objectName: "importCard"
                                width: parent.width
                                iconKind: "file"
                                text: qsTr("Import a setup file")
                                onClicked: root.importRequested()
                            }

                            PrimaryButton {
                                objectName: "manualCard"
                                width: parent.width
                                variant: PrimaryButton.Secondary
                                text: qsTr("Set up by hand")
                                onClicked: stack.push(manualPage)
                            }

                            Text {
                                objectName: "setupFileNote"
                                x: Theme.space.tileGap
                                width: parent.width - 2 * x
                                topPadding: Theme.space.tileGap / 2
                                horizontalAlignment: Text.AlignHCenter
                                color: Theme.colour.textSecondary
                                font: Theme.font.footnote.font
                                lineHeightMode: Text.FixedHeight
                                lineHeight: Theme.font.footnote.linePitch
                                text: qsTr("A setup file holds your error types, locations and note templates. You can import one later in Settings.")
                                wrapMode: Text.WordWrap
                                Accessible.role: Accessible.StaticText
                                Accessible.name: text
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
            onBackRequested: stack.pop()
            onCompleted: root.completed()
        }
    }
}
