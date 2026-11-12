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

    // The margin on each side of the content: the phone's screen margin, or wider so that the content stays in the
    // form column of a larger window (Stage 8, docs/plan/stage-8.md, "Layout rules").
    readonly property real side: WindowLayout.sideMargin(width, Theme.layout.formColumn)

    required property string title
    // The top safe-area inset, from the SafeFrame of SettingsScreen.
    required property real topInset
    // The leading action ("Settings", with a chevron) and the trailing one ("Save"). Empty for none.
    property string leadingText
    property string trailingText
    property bool trailingEnabled: true
    // What a screen reader says for the help button of the title bar, such as "Help with the board layout". Empty for none.
    property string helpName
    // Whether a drag scrolls the page. The list turns it off while a tile of the board is being dragged.
    property bool scrollable: true
    // Room above the title and between the title and the content, for a page whose title has no actions above it (the
    // Settings list: BoardSettings has 8 pt above "Settings" and 16 pt under it). The NavBar adds its own 8 pt under a
    // large title, so `bodyGap` is what comes on top of that.
    property real topGap: 0
    property real bodyGap: 0
    // The space between the blocks of the content (the section gap of the design; 8 pt on the import page).
    property real bodySpacing: Theme.space.sectionGap
    // Above this text scale the leading action says "Back", so that it fits beside its chevron; a screen reader says where
    // it goes at every size.
    readonly property real compactScale: 2

    readonly property Flickable flickable: flick

    default property alias content: body.data

    signal leadingTriggered
    signal trailingTriggered
    signal helpRequested

    color: Theme.colour.background

    // The page's own spacer keeps the content under the top inset, so this frame takes no inset: it is here for the keyboard,
    // which ends the page at its top and scrolls the field being typed in into view.
    SafeFrame {
        anchors.fill: parent
        overrideInsets: true
        avoidKeyboard: true

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

                Item {
                    objectName: "settingsTitleGap"
                    width: parent.width
                    height: root.topGap
                }

                NavBar {
                    objectName: "settingsNavBar"
                    width: parent.width
                    sideInset: root.side
                    title: root.title
                    large: true
                    leadingText: root.leadingText.length > 0 && Theme.clampedScale > root.compactScale ? qsTr("Back") : root.leadingText
                    leadingAccessibleName: qsTr("Back to %1").arg(root.leadingText)
                    leadingBack: root.leadingText.length > 0
                    trailingText: root.trailingText
                    trailingEnabled: root.trailingEnabled
                    helpName: root.helpName
                    onHelpTriggered: root.helpRequested()
                    onLeadingTriggered: root.leadingTriggered()
                    onTrailingTriggered: root.trailingTriggered()
                }

                Column {
                    id: body

                    x: root.side
                    width: parent.width - 2 * root.side
                    topPadding: root.bodyGap
                    spacing: root.bodySpacing
                }
            }
        }
    }
}
