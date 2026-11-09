// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The frame of an editor page and of the first-launch pages: the screen background, a navigation bar with the large
// title and, below it, the area for the page's own content. The bar sits inside the safe area (SafeFrame is the only
// reader of the insets), and the content area fills what is left, so a page that scrolls puts a Flickable in it.
//
// This file is a part of the editors and of the setup pages and is tested through them.
Rectangle {
    id: root

    required property string title
    // The leading action ("Settings", "Note templates"). Above this text scale it says "Back", so that it fits beside its
    // chevron; a screen reader says where it goes at every size.
    readonly property real compactScale: 2
    property string leadingText
    property string leadingAccessibleName: root.leadingText
    property bool leadingBack: true
    property string trailingText
    property string trailingAccessibleName: root.trailingText
    property bool trailingEnabled: true

    // The page's content: below the bar, in the safe area.
    default property alias content: body.data
    readonly property Item contentArea: body

    signal leadingTriggered
    signal trailingTriggered

    color: Theme.colour.background

    SafeFrame {
        id: frame

        anchors.fill: parent
        // The page ends at the top of the keyboard, the bar stays where it is, and the field being typed in is scrolled
        // into view.
        avoidKeyboard: true

        NavBar {
            id: nav

            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            large: true
            title: root.title
            leadingText: root.leadingText.length > 0 && Theme.clampedScale > root.compactScale ? qsTr("Back") : root.leadingText
            leadingAccessibleName: root.leadingAccessibleName
            leadingBack: root.leadingBack
            trailingText: root.trailingText
            trailingAccessibleName: root.trailingAccessibleName
            trailingEnabled: root.trailingEnabled
            onLeadingTriggered: root.leadingTriggered()
            onTrailingTriggered: root.trailingTriggered()
        }

        Item {
            id: body

            objectName: "editorPageContent"
            anchors.top: nav.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
        }
    }
}
