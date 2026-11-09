// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The About page (PRF-12): what this build is, the licence of the app, where its source is, and the open-source components it
// links, each with its licence. It is pushed from the "About CS Bingo Board" row of the Settings list.
//
//   this build   the name, the version, the build number, the commit, the branch or tag when there is one, the date of the commit
//                (UTC), the platform, the Qt version and the storage. All of it comes from `about` (AboutModel), which has it from
//                the build (tools/licences/build_info.py) and not from anything typed here.
//   licence      its name, and a row that opens the full text (`licenceRequested("app")`)
//   source code  the address of the repository; the row opens it in the browser
//   components   the list that the build made from the real link line, each row opening the licence text of that component
//
// Five quick taps on the Version row show or hide the timing line the exit gate is read from on a phone (Stage 5, "Timing marks"):
// `launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms`. There is no Mac to read the marks with, so this line is how
// the numbers get off a TestFlight build. The row is read as text, not as a control, and says nothing of the gesture.
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var about
    // The timing line from the bridge.
    property string timingText: ""
    // Whether the timing line is shown.
    property bool timingShown: false

    // The taps that show or hide the line, counted by the pointer handler.
    readonly property int tapsToToggle: 5

    // The page was left.
    signal closeRequested
    // A licence text was asked for: "app", or the id of a component.
    signal licenceRequested(string licenceId)
    // An address is to be opened in the browser: the shell does it, so that a test can see the request and open nothing.
    signal openUrlRequested(string url)

    title: qsTr("About")
    leadingText: qsTr("Settings")
    bodySpacing: Theme.space.tileGap
    onLeadingTriggered: closeRequested()

    Banner {
        objectName: "aboutProblem"
        width: parent.width
        visible: root.about.problem.length > 0
        title: root.about.problem
        tone: Banner.Warning
        closable: false
    }

    GroupedList {
        objectName: "aboutBuild"
        width: parent.width
        title: qsTr("This build")

        ListRow {
            objectName: "aboutName"
            text: qsTr("App")
            value: root.about.appName
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutVersion"
            text: qsTr("Version")
            value: root.about.version
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact

            TapHandler {
                onTapped: {
                    if (tapCount >= root.tapsToToggle)
                        root.timingShown = !root.timingShown;
                }
            }
        }

        ListRow {
            objectName: "aboutBuildNumber"
            text: qsTr("Build")
            value: root.about.buildNumber
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutCommit"
            text: qsTr("Commit")
            value: root.about.commit
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutRef"
            visible: root.about.ref.length > 0
            text: qsTr("Branch or tag")
            value: root.about.ref
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutBuildDate"
            text: qsTr("Built")
            value: root.about.buildDate
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutPlatform"
            text: qsTr("Platform")
            value: root.about.platform
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutQt"
            text: qsTr("Qt")
            value: root.about.qtVersion
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutStorage"
            text: qsTr("Storage")
            value: root.about.storage
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }
    }

    Text {
        objectName: "settingsTiming"
        width: parent.width
        visible: root.timingShown && root.timingText.length > 0
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.colour.textSecondary
        font: Theme.font.caption.font
        text: root.timingText
    }

    // 12 pt more than the 8 pt between blocks, as the mock-up has it above each heading.
    Item {
        width: parent.width
        height: Theme.space.sectionGap
    }

    GroupedList {
        objectName: "aboutLicence"
        width: parent.width
        title: qsTr("Licence")
        footer: qsTr("CS Bingo Board is free software. You may copy it, change it and share it under the terms of this licence.")

        ListRow {
            objectName: "aboutLicenceName"
            text: qsTr("Licence")
            value: root.about.licenceName
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "aboutLicenceText"
            text: qsTr("Read the licence")
            accessibleName: qsTr("Read the licence, %1").arg(root.about.licenceName)
            showChevron: true
            minimumHeight: Theme.size.rowHeightCompact
            onActivated: root.licenceRequested("app")
        }
    }

    // 12 pt more than the 8 pt between blocks, as the mock-up has it above each heading.
    Item {
        width: parent.width
        height: Theme.space.sectionGap
    }

    GroupedList {
        objectName: "aboutSource"
        width: parent.width
        title: qsTr("Source code")

        ListRow {
            objectName: "aboutSourceLink"
            text: qsTr("Open the source code")
            accessibleName: qsTr("Open the source code, %1").arg(root.about.sourceUrl)
            labelColour: Theme.colour.primaryText
            minimumHeight: Theme.size.rowHeightCompact
            onActivated: root.openUrlRequested(root.about.sourceUrl)
        }
    }

    Text {
        objectName: "aboutSourceUrl"
        x: Theme.space.tileGap / 2
        width: parent.width - 2 * x
        color: Theme.colour.textSecondary
        font: Theme.font.footnote.font
        text: root.about.sourceUrl
        wrapMode: Text.WrapAtWordBoundaryOrAnywhere
        Accessible.ignored: true
    }

    // 12 pt more than the 8 pt between blocks, as the mock-up has it above each heading.
    Item {
        width: parent.width
        height: Theme.space.sectionGap
        visible: root.about.components.length > 0
    }

    GroupedList {
        objectName: "aboutComponents"
        width: parent.width
        title: qsTr("Open-source components")
        footer: qsTr("What this app links, and what comes with it. Each page has the licence text.")
        visible: root.about.components.length > 0

        Repeater {
            model: root.about.components

            SettingsComponentRow {
                required property var modelData

                objectName: "aboutComponent"
                name: modelData.name
                version: modelData.version
                licence: modelData.licence
                onActivated: root.licenceRequested(modelData.id)
            }
        }
    }
}
