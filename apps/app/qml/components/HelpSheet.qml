// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The help sheet (Stage 10, package 10.6; mock-ups HelpSheet and HelpSheetLarge): a topic of HelpTopics as a heading, its
// paragraphs and, under them, "Read more in the guide". The text is in the app and shows without a connection; the button
// only asks for the guide's address, which the shell opens in the browser, and a line under it says so.
//
//     HelpSheet { id: help; onGuideRequested: url => opener.open(url) }
//     ...
//     help.show("board")
//
// The sheet is as tall as its text (Sheet.Content) and scrolls when the text is taller than the window allows, which it is
// at the larger text sizes. The paragraphs are plain text items in reading order, so a screen reader reads them one after the
// other, and they wrap at every text size. Nothing relies on colour: the button has a label and an icon.
Sheet {
    id: root

    // The id of the topic shown, empty when none.
    property string topicId
    readonly property var topic: HelpTopics.topic(topicId)
    readonly property string heading: topic ? topic.title : ""

    // "Read more in the guide" was activated: the address of the section of the guide that the topic names.
    signal guideRequested(string url)

    // Presents the sheet with the topic `id`. An id that is not a topic shows nothing and does not open.
    function show(id: string): void {
        if (HelpTopics.topic(id) === null)
            return;
        topicId = id;
        flick.contentY = 0;
        open();
    }

    accessibleName: root.heading
    detent: Sheet.Content
    contentImplicitHeight: Math.min(bar.height + flick.contentHeight + footer.height, root.available - root.grabberZone - root.bottomInset)

    Item {
        anchors.fill: parent

        NavBar {
            id: bar

            objectName: "helpBar"
            width: parent.width
            inSheet: true
            title: root.heading
            trailingText: qsTr("Done")
            onTrailingTriggered: root.close()
        }

        Flickable {
            id: flick

            objectName: "helpFlickable"
            y: bar.height
            width: parent.width
            height: parent.height - y - footer.height
            contentWidth: width
            contentHeight: paragraphs.height + Theme.space.sectionGap
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Column {
                id: paragraphs

                objectName: "helpParagraphs"
                x: Theme.space.screenMargin + Theme.space.tileGap / 2
                width: flick.width - 2 * x
                spacing: Theme.space.tileGap

                Repeater {
                    model: root.topic ? root.topic.paragraphs : []

                    delegate: Text {
                        required property string modelData

                        width: paragraphs.width
                        wrapMode: Text.WordWrap
                        textFormat: Text.StyledText
                        color: Theme.colour.textPrimary
                        font: Theme.font.body.font
                        text: modelData
                    }
                }
            }
        }

        Column {
            id: footer

            objectName: "helpFooter"
            anchors.bottom: parent.bottom
            x: Theme.space.screenMargin
            width: parent.width - 2 * x
            spacing: Theme.space.tileGap
            topPadding: Theme.space.tileGap

            Item {
                id: guideButton

                objectName: "helpReadMore"
                width: parent.width
                height: Math.max(Theme.size.minTouchTarget, label.implicitHeight + Theme.space.sectionGap)
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Read more in the guide")
                Accessible.description: qsTr("Opens the guide in your browser")
                Accessible.onPressAction: root.guideRequested(HelpTopics.topicAddress(root.topicId))

                TapHandler {
                    onTapped: root.guideRequested(HelpTopics.topicAddress(root.topicId))
                }

                Rectangle {
                    anchors.fill: parent
                    radius: Theme.radius.primaryButton
                    color: Theme.colour.primaryTint
                    border.width: Theme.border.hairline
                    border.color: Theme.colour.border
                    Accessible.ignored: true
                }

                Row {
                    anchors.centerIn: parent
                    spacing: Theme.space.tileGap

                    Text {
                        id: label

                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, guideButton.width - Theme.size.badge - 3 * Theme.space.tileGap)
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                        color: Theme.colour.primaryText
                        font: Theme.font.bodySmall.selectedFont
                        text: qsTr("Read more in the guide")
                        Accessible.ignored: true
                    }

                    Icon {
                        anchors.verticalCenter: parent.verticalCenter
                        kind: "external"
                        size: Theme.size.badge * 3 / 4
                        colour: Theme.colour.primaryText
                    }
                }
            }

            Text {
                objectName: "helpOfflineNote"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.colour.textSecondary
                font: Theme.font.footnote.font
                text: qsTr("The guide opens in your browser. This help works without a connection.")
            }

            Item {
                width: parent.width
                height: Theme.space.tileGap
            }
        }
    }
}
