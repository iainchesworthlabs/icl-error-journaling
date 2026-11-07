// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// An inline message with a tone, an optional action and a close button (Stage 4, package 4.4). The write-failed and
// recovery messages of the Log screen are banners: the words come from the view models and arrive here as `title` and
// `message`, so the component has no wording of its own beyond the label of the close button.
//
// Each tone has a fill, an outline and an icon of its own, so the tone is not carried by colour alone: information is a
// circle with an "i" on the deep blue tint, warning and error are a triangle with an exclamation mark on amber and red.
// Place the banner with anchors left and right and let its height follow its text.
//
// To a screen reader the banner is an alert message named by its title and described by its message, followed by its
// close button and its action. The tests of the rig load it into a window-sized item; the card sits at the top of it.
Item {
    id: root

    enum Tone {
        Information,
        Warning,
        Error
    }

    // The first line, in the heavier weight.
    required property string title
    // The explanation under it. Empty for a title-only banner.
    property string message
    property int tone: Banner.Information
    // The text of the optional action, such as "Check the log in Review". Empty for none.
    property string actionText
    // Whether the close button is shown.
    property bool closable: true
    // The accessible name of the close button.
    property string closeLabel: qsTr("Dismiss")
    // The tallest the banner may be. At the largest text sizes a long message is taller than the window, and the text then
    // scrolls inside the banner, with the close button staying where it is. A screen sets it to what it can spare.
    property real maximumHeight: Infinity

    // The action was activated.
    signal actionTriggered
    // The close button was activated. The banner does not hide itself: its owner decides.
    signal closeRequested

    readonly property color fillColour: tone === Banner.Warning ? Theme.colour.bannerWarning : tone === Banner.Error ? Theme.colour.bannerError : Theme.colour.primaryTint
    // The outline and the icon.
    readonly property color accentColour: tone === Banner.Warning ? Theme.colour.bannerWarningBorder : tone === Banner.Error ? Theme.colour.bannerErrorBorder : Theme.colour.primaryText
    readonly property string iconPath: tone === Banner.Information ? "M12 21A9 9 0 1 0 12 3A9 9 0 0 0 12 21Z M12 11V16 M12 8H12.01" : "M12 3L2 21H22L12 3Z M12 10V15 M12 18H12.01"
    // The width the text column keeps clear on the right for the close button.
    readonly property real closeWidth: closable ? Theme.size.minTouchTarget : 0

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: card.height

    Rectangle {
        id: card

        objectName: "bannerCard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.min(root.maximumHeight, Math.max(textColumn.height + 2 * Theme.space.sectionGap, root.closable ? Theme.size.minTouchTarget : 0))
        radius: Theme.radius.toast
        color: root.fillColour
        border.width: Theme.border.otherWidth
        border.color: root.accentColour
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.title
        Accessible.description: root.message

        Shape {
            id: icon

            x: Theme.space.rowPadding
            y: Theme.space.sectionGap
            width: Theme.size.badge
            height: Theme.size.badge
            Accessible.ignored: true

            ShapePath {
                strokeColor: root.accentColour
                strokeWidth: Theme.border.hairline * 2
                fillColor: "transparent"
                capStyle: ShapePath.RoundCap
                joinStyle: ShapePath.RoundJoin

                PathSvg {
                    path: root.iconPath
                }
            }
        }

        // The close button comes before the action in reading order because it sits above it.
        Item {
            id: closeButton

            objectName: "bannerClose"
            visible: root.closable
            anchors.top: parent.top
            anchors.right: parent.right
            width: Theme.size.minTouchTarget
            height: Theme.size.minTouchTarget
            Accessible.role: Accessible.Button
            Accessible.name: root.closeLabel
            Accessible.onPressAction: root.closeRequested()

            TapHandler {
                enabled: root.closable
                onTapped: root.closeRequested()
            }

            Shape {
                anchors.centerIn: parent
                width: Theme.size.badge
                height: Theme.size.badge
                Accessible.ignored: true

                ShapePath {
                    strokeColor: Theme.colour.textSecondary
                    strokeWidth: Theme.border.hairline * 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap

                    PathSvg {
                        path: "M7 7L17 17M17 7L7 17"
                    }
                }
            }
        }

        // The text, with the action under it. It scrolls only when the banner has reached `maximumHeight`.
        Flickable {
            id: textFlick

            x: Theme.space.rowPadding + Theme.size.badge + Theme.space.sectionGap
            y: Theme.space.sectionGap
            width: card.width - x - Theme.space.rowPadding - root.closeWidth
            height: card.height - 2 * Theme.space.sectionGap
            clip: true
            contentWidth: width
            contentHeight: textColumn.height
            interactive: contentHeight > height
            boundsBehavior: Flickable.StopAtBounds

            Column {
                id: textColumn

                width: textFlick.width

                Text {
                    objectName: "bannerTitle"
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.colour.textPrimary
                    font: Qt.font({
                        "pixelSize": Theme.font.bodySmall.font.pixelSize,
                        "weight": Font.DemiBold
                    })
                    text: root.title
                    Accessible.ignored: true
                }

                Text {
                    objectName: "bannerMessage"
                    visible: root.message.length > 0
                    width: parent.width
                    wrapMode: Text.WordWrap
                    color: Theme.colour.textPrimary
                    font: Theme.font.bodySmall.font
                    text: root.message
                    Accessible.ignored: true
                }

                Item {
                    id: action

                    objectName: "bannerAction"
                    visible: root.actionText.length > 0
                    width: parent.width
                    height: visible ? Math.max(Theme.size.minTouchTarget, actionLabel.implicitHeight) : 0
                    Accessible.role: Accessible.Button
                    Accessible.name: root.actionText
                    Accessible.onPressAction: root.actionTriggered()

                    TapHandler {
                        enabled: action.visible
                        onTapped: root.actionTriggered()
                    }

                    Text {
                        id: actionLabel

                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        wrapMode: Text.WordWrap
                        color: Theme.colour.primaryText
                        font: Qt.font({
                            "pixelSize": Theme.font.bodySmall.font.pixelSize,
                            "weight": Font.DemiBold,
                            "underline": true
                        })
                        text: root.actionText
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
}
