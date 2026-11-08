// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The modal alert of the design (Stage 4, package 4.4): the new-day prompt, the start-a-new-log confirmation and the
// destructive confirmations. A 22 pt-radius card over a dimmed window with an icon, a title, a message and up to three
// buttons:
//
//   confirm    the default action, a filled button; filled red (`destructive`) when it deletes something
//   alternate  a second, softer action, such as "Share this log first"
//   cancel     the plain text button that leaves things as they are
//
// An alert is not dismissed by a tap outside it, by a drag or by Escape: the person chooses a button. Each button closes
// the alert and then emits its signal, so the owner needs no extra code to hide it. Open it with `open()`; focus goes to
// it, stays in it and returns to where it was when it closes. It is a Sheet in its centred presentation, so it is shown
// over the whole window when it is declared in a SheetHost.
//
// The layout is the mock-ups': the card hung from Theme.size.alertAnchor (see Sheet) with the alert shadow, a 26 pt icon in
// a 48 pt disc, 10 pt between the icon, the title and the message, the message in 15 pt with a 1.45 line height, 20 pt
// before the buttons, the filled buttons 50 pt high and the plain cancel button 46 pt, 8 pt apart.
//
// The wording is the owner's: the card has no strings of its own.
Sheet {
    id: root

    enum Glyph {
        None,
        Calendar,
        Trash,
        Warning
    }

    required property string title
    property string message
    property int icon: Alert.None
    // The text of the default button. Empty for none.
    property string confirmText
    // Whether the default button deletes something: it is then red and is not the default button for a screen reader.
    property bool destructive: false
    property string alternateText
    property string cancelText

    signal confirmed
    signal alternated
    signal cancelled

    readonly property bool alarming: destructive || icon === Alert.Warning
    readonly property color iconFill: alarming ? Theme.colour.bannerError : Theme.colour.primaryTint
    readonly property color iconStroke: alarming ? Theme.colour.destructive : Theme.colour.primaryText
    readonly property string iconKind: icon === Alert.Trash ? "trash" : icon === Alert.Warning ? "warning" : "calendar"

    accessibleName: root.title
    accessibleDescription: root.message
    presentation: Sheet.Centre
    dismissible: false
    scrimColour: Theme.colour.alertScrim
    contentImplicitHeight: column.implicitHeight

    // One button of the card.
    component AlertButton: Item {
        id: button

        required property string label
        property color fill: "transparent"
        property color labelColour: Theme.colour.primaryText
        property bool emphasised: false
        property bool isDefault: false
        property real buttonHeight: Theme.size.alertButtonHeight
        readonly property bool pressed: tap.pressed

        signal triggered

        width: parent ? parent.width : 0
        height: Math.max(buttonHeight, buttonLabel.implicitHeight + 2 * Theme.space.tileGap)
        activeFocusOnTab: true
        scale: tap.pressed ? Theme.motion.tilePressScale : 1
        Accessible.role: Accessible.Button
        Accessible.name: button.label
        Accessible.defaultButton: button.isDefault
        Accessible.focusable: true
        Accessible.onPressAction: button.triggered()
        Keys.onReturnPressed: button.triggered()
        Keys.onSpacePressed: button.triggered()

        TapHandler {
            id: tap

            onTapped: button.triggered()
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radius.primaryButton
            color: button.fill
            Accessible.ignored: true
        }

        Text {
            id: buttonLabel

            anchors.centerIn: parent
            width: parent.width - 2 * Theme.space.screenMargin
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            color: button.labelColour
            font: button.emphasised ? Theme.font.body.selectedFont : Theme.font.body.font
            text: button.label
            Accessible.ignored: true
        }
    }

    Flickable {
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: column.implicitHeight
        interactive: contentHeight > height
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: column

            width: parent.width
            spacing: Theme.space.tileGap + Theme.border.hairline * 2

            Rectangle {
                objectName: "alertIcon"
                anchors.horizontalCenter: parent.horizontalCenter
                width: Theme.size.listRowHeight
                height: width
                radius: width / 2
                color: root.iconFill
                Accessible.ignored: true

                Icon {
                    objectName: "alertIconGlyph"
                    anchors.centerIn: parent
                    size: Theme.size.badge * 13 / 12
                    kind: root.iconKind
                    colour: root.iconStroke
                }
            }

            Text {
                objectName: "alertTitle"
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.colour.textPrimary
                font: Theme.font.headerUnit.font
                text: root.title
                Accessible.ignored: true
            }

            Text {
                objectName: "alertMessage"
                visible: root.message.length > 0
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                color: Theme.colour.textBody
                font: Theme.font.alertMessage.font
                lineHeightMode: Text.FixedHeight
                lineHeight: Theme.font.alertMessage.linePitch
                text: root.message
                Accessible.ignored: true
            }

            // The buttons, 10 pt further down than the column's gap.
            Column {
                objectName: "alertButtons"
                width: parent.width
                topPadding: Theme.space.tileGap + Theme.border.hairline * 2
                spacing: Theme.space.tileGap

                AlertButton {
                    id: confirmButton

                    objectName: "alertConfirm"
                    visible: root.confirmText.length > 0
                    label: root.confirmText
                    fill: root.destructive ? Theme.colour.destructiveFill : confirmButton.pressed ? Theme.colour.primaryPressed : Theme.colour.primary
                    labelColour: root.destructive ? Theme.colour.textOnDestructive : Theme.colour.textOnPrimary
                    emphasised: true
                    isDefault: !root.destructive
                    onTriggered: {
                        root.close();
                        root.confirmed();
                    }
                }

                AlertButton {
                    objectName: "alertAlternate"
                    visible: root.alternateText.length > 0
                    label: root.alternateText
                    fill: Theme.colour.primaryTint
                    emphasised: true
                    onTriggered: {
                        root.close();
                        root.alternated();
                    }
                }

                AlertButton {
                    objectName: "alertCancel"
                    visible: root.cancelText.length > 0
                    label: root.cancelText
                    buttonHeight: Theme.size.alertCancelHeight
                    onTriggered: {
                        root.close();
                        root.cancelled();
                    }
                }
            }
        }
    }
}
