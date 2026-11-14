// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The skeleton of the Log screen (Stage 5, package 5.8): the picture that the launch screen shows until the first frame of
// the application. The background, the header card, the note composer, the "Last" bar, the 12 tiles in their 3 by 4 grid and
// the tab bar, as plain shapes in neutral colours, with no text, no badge and no colour per type: the order and the colours
// of the tiles are the tester's setup, which the launch screen cannot know.
//
// It lays out the same components as the Log screen and the shell do (HeaderCard, NoteComposer, LastEntryBar, BoardGrid,
// TabBar), each in its `skeleton` mode, with the same margins and gaps (the placing below repeats the lines of
// screens/log/LogScreen.qml) and the safe-area insets of the design frame (59 pt at the top, 34 pt at the bottom). So the
// picture cannot drift from the screen: the tool `ej_launch_image` renders this item in light and dark
// into the PNGs of the asset catalogue, CI renders them again and compares, and the test `launch.matches_screen` holds the
// rectangles of the tiles, the header card and the tab bar to those of the real Log screen.
//
// It is the size of the design frame, 402 x 874 pt. The colours follow `Theme`, so the appearance is the one the theme is in.
Item {
    id: root

    // What the tiles are made from: twelve rows with no type. The skeleton draws no text and no colour from them.
    readonly property var tileRows: Array.from({
        "length": Theme.size.boardColumns * Theme.size.boardRows
    }, () => ({
                "typeId": "",
                "label": "",
                "colourSlot": 0,
                "count": 0,
                "hasCount": false,
                "isOther": false,
                "isEmpty": false,
                "accessibleName": ""
            }))
    // What stands in for an empty line of the header card, as on the Log screen while it loads: one short mark in the font of
    // the card, so that no line wraps and the card has the height it will have once the values are in.
    readonly property string loadingMark: "-"

    implicitWidth: Theme.frame.width
    implicitHeight: Theme.frame.height
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        objectName: "launchBackground"
        anchors.fill: parent
        color: Theme.colour.background
    }

    SafeFrame {
        id: safe

        objectName: "launchSafeFrame"
        anchors.fill: parent
        overrideInsets: true
        overrideTop: Theme.frame.safeAreaTop
        overrideBottom: Theme.frame.homeIndicator

        HeaderCard {
            id: header

            objectName: "launchHeaderCard"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            unitText: ""
            locationText: ""
            versionText: ""
            dateText: root.loadingMark
            testerText: ""
            missingFields: []
            unitPrompt: root.loadingMark
            locationPrompt: root.loadingMark
            versionPrompt: root.loadingMark
            testerPrompt: root.loadingMark
            skeleton: true
            nominalMetrics: true
        }

        NoteComposer {
            id: composer

            objectName: "launchComposer"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            anchors.top: header.bottom
            anchors.topMargin: Theme.space.sectionGap
            draft: ""
            chips: []
            skeleton: true
            nominalMetrics: true
        }

        LastEntryBar {
            id: lastBar

            objectName: "launchLastBar"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            anchors.top: composer.bottom
            anchors.topMargin: Theme.space.sectionGap
            lastLine: ""
            entryCount: 0
            skeleton: true
        }

        BoardGrid {
            objectName: "launchBoard"
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            anchors.top: lastBar.bottom
            anchors.topMargin: Theme.space.sectionGap
            model: root.tileRows
            skeleton: true
        }
    }

    TabBar {
        objectName: "launchTabBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        bottomInset: safe.bottomInset
        skeleton: true
    }
}
