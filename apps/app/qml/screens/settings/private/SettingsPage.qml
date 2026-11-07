// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The frame of each page of Settings (Stage 4, package 4.12): the screen colour, a NavBar with the large title, and a column
// that scrolls under the top inset. A page puts its content in the column; at the larger text sizes the page scrolls.
//
// The inset is handed down by SettingsScreen, which owns the SafeFrame. This file is a part of the screen and is tested
// through it.
Rectangle {
    id: root

    required property string title
    // The top safe-area inset, from the SafeFrame of SettingsScreen.
    required property real topInset
    // The leading action ("Settings", with a chevron) and the trailing one ("Save"). Empty for none.
    property string leadingText
    property string trailingText
    property bool trailingEnabled: true
    // Whether a drag scrolls the page. The list turns it off while a tile of the board is being dragged.
    property bool scrollable: true
    // Above this text scale the leading action says "Back", so that it fits beside its chevron; a screen reader says where
    // it goes at every size.
    readonly property real compactScale: 2

    readonly property Flickable flickable: flick

    default property alias content: body.data

    signal leadingTriggered
    signal trailingTriggered

    color: Theme.colour.background

    Flickable {
        id: flick

        objectName: "settingsFlickable"
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.height + Theme.space.screenMargin
        interactive: root.scrollable
        boundsBehavior: Flickable.StopAtBounds
        clip: true

        Column {
            id: column

            width: flick.width

            // The content starts below the inset and scrolls under it; a spacer and not a margin of the Flickable, so that
            // an inset that arrives late moves the content and not the scroll position.
            Item {
                objectName: "settingsTopInset"
                width: parent.width
                height: root.topInset
            }

            NavBar {
                objectName: "settingsNavBar"
                width: parent.width
                title: root.title
                large: true
                leadingText: root.leadingText.length > 0 && Theme.clampedScale > root.compactScale ? qsTr("Back") : root.leadingText
                leadingAccessibleName: qsTr("Back to %1").arg(root.leadingText)
                leadingBack: root.leadingText.length > 0
                trailingText: root.trailingText
                trailingEnabled: root.trailingEnabled
                onLeadingTriggered: root.leadingTriggered()
                onTrailingTriggered: root.trailingTriggered()
            }

            Column {
                id: body

                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                spacing: Theme.space.sectionGap
            }
        }
    }
}
