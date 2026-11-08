// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
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
//
// Look (WriteFailed, Recovery, ImportConfirm): a 1.5 pt outline, 12 pt and 14 pt padding inside it, a 22 pt icon 1 pt
// below the top of the text and 10 pt before it, the title in 15/600 with a 1.3 line height, the message in 14 pt with a
// 1.4 line height 2 pt under it, and a 12 pt close cross centred 27.5 pt from the right edge and 25 pt from the top.
// `floating` adds the toast shadow, for a banner that floats over the Log screen; an inline banner has none.
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
    // The banner floats over the screen and casts the toast shadow.
    property bool floating: false

    // The action was activated.
    signal actionTriggered
    // The close button was activated. The banner does not hide itself: its owner decides.
    signal closeRequested

    readonly property color fillColour: tone === Banner.Warning ? Theme.colour.bannerWarning : tone === Banner.Error ? Theme.colour.bannerError : Theme.colour.primaryTint
    // The outline and the icon.
    readonly property color accentColour: tone === Banner.Warning ? Theme.colour.bannerWarningBorder : tone === Banner.Error ? Theme.colour.bannerErrorBorder : Theme.colour.primaryText
    readonly property string iconKind: tone === Banner.Information ? "info" : "warning"
    // The inside of the outline, where the padding starts.
    readonly property real edge: Theme.border.otherWidth
    readonly property real iconSize: Theme.size.badge * 11 / 12
    // The close button is a 44 pt target that reaches 10 pt into the padding at the top right corner.
    readonly property real closeReach: Theme.space.tileGap + Theme.border.hairline * 2
    // The width the text column keeps clear on the right for the close button.
    readonly property real closeWidth: closable ? Theme.size.minTouchTarget - closeReach : 0

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: card.height

    Shadow {
        visible: root.floating
        anchors.fill: card
        radius: card.radius
        token: Theme.shadow.toast
    }

    Rectangle {
        id: card

        objectName: "bannerCard"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: Math.min(root.maximumHeight, Math.max(textColumn.height + 2 * (Theme.space.sectionGap + root.edge), root.closable ? Theme.size.minTouchTarget : 0))
        radius: Theme.radius.toast
        color: root.fillColour
        border.width: Theme.border.otherWidth
        border.color: root.accentColour
        Accessible.role: Accessible.AlertMessage
        Accessible.name: root.title
        Accessible.description: root.message

        Icon {
            id: icon

            objectName: "bannerIcon"
            x: root.edge + Theme.space.rowPadding
            y: root.edge + Theme.space.sectionGap + Theme.border.hairline
            size: root.iconSize
            kind: root.iconKind
            colour: root.accentColour
        }

        // The close button comes before the action in reading order because it sits above it.
        Item {
            id: closeButton

            objectName: "bannerClose"
            visible: root.closable
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.topMargin: root.edge + Theme.space.sectionGap - root.closeReach
            anchors.rightMargin: root.edge + Theme.space.rowPadding - root.closeReach
            width: Theme.size.minTouchTarget
            height: Theme.size.minTouchTarget
            Accessible.role: Accessible.Button
            Accessible.name: root.closeLabel
            Accessible.onPressAction: root.closeRequested()

            TapHandler {
                enabled: root.closable
                onTapped: root.closeRequested()
            }

            Icon {
                objectName: "bannerCloseIcon"
                anchors.centerIn: parent
                size: Theme.size.badge * 3 / 4
                kind: "close"
                colour: Theme.colour.textSecondary
            }
        }

        // The text, with the action under it. It scrolls only when the banner has reached `maximumHeight`.
        Flickable {
            id: textFlick

            x: icon.x + icon.width + Theme.space.tileGap + Theme.border.hairline * 2
            y: root.edge + Theme.space.sectionGap
            width: card.width - x - root.edge - Theme.space.rowPadding - root.closeWidth
            height: card.height - 2 * y
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
                    font: Theme.font.bannerTitle.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.bannerTitle.linePitch
                    text: root.title
                    Accessible.ignored: true
                }

                Text {
                    objectName: "bannerMessage"
                    visible: root.message.length > 0
                    width: parent.width
                    // 2 pt under the title.
                    topPadding: Theme.border.hairline * 2
                    wrapMode: Text.WordWrap
                    color: Theme.colour.textPrimary
                    font: Theme.font.messageText.font
                    lineHeightMode: Text.FixedHeight
                    lineHeight: Theme.font.messageText.linePitch
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
                        font: Theme.font.bodySmall.linkFont
                        text: root.actionText
                        Accessible.ignored: true
                    }
                }
            }
        }
    }
}
