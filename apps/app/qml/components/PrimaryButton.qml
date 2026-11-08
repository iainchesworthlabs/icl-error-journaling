// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The buttons of the design: 54 pt high, 14 pt radius and a 17/600 label, in three variants.
//
//   variant   PrimaryButton.Primary    the deep blue fill and a white label ("Start logging", "Share log")
//             PrimaryButton.Secondary  the primaryTint fill and a primaryText label ("Set up by hand")
//             PrimaryButton.Plain      no fill and a primaryText label ("Cancel" under a primary button); it replaces
//                                      the TextButton of the settings editors
//   iconKind  an Icon kind drawn 20 pt before the label with an 8 pt gap, in the label's colour ("share" for Share log,
//             "file" for Import a setup file). Empty for none.
//   compact   the 44 pt button beside a field (the composer's "Add"): 12 pt radius, a 15/600 label, at least 64 pt wide.
//   height    the button is as high as its variant (54, or 44 compact) and grows with its label; set `height` for the
//             48 pt (Theme.size.smallButtonHeight) and 50 pt (Theme.size.alertButtonHeight) buttons of some screens.
//
// It is a plain item with a tap handler and not a Basic control, because it supplies no behaviour a control would: the
// press state is the handler's, the activation is the `clicked` signal, and the accessibility press action and the
// Return and Space keys call the same signal, so every way to activate it is one path. A disabled button is reported as
// disabled and does not react. It is dimmed only when it is disabled itself: a sheet over the screen disables the screen
// (SheetHost), and the buttons under the scrim keep their look.
Item {
    id: root

    enum Variant {
        Primary,
        Secondary,
        Plain
    }

    required property string text
    property int variant: PrimaryButton.Primary
    property string iconKind: ""
    property bool compact: false
    // What a screen reader says; the label by default.
    property string accessibleName: text

    readonly property bool pressed: tap.pressed
    // Disabled by its own `enabled`, not only by a disabled ancestor (the screen under a sheet).
    readonly property bool ownDisabled: !enabled && (parent === null || parent.enabled)
    readonly property bool hasIcon: iconKind.length > 0
    readonly property color labelColour: variant === PrimaryButton.Primary ? Theme.colour.textOnPrimary : Theme.colour.primaryText
    readonly property real iconSize: Theme.size.badge * 5 / 6

    signal clicked

    implicitWidth: Math.max(content.implicitWidth + 2 * Theme.space.screenMargin, compact ? Theme.size.tileMinimum * 8 / 11 : Theme.size.minTouchTarget * 1.5)
    implicitHeight: Math.max(compact ? Theme.size.fieldHeight : Theme.size.primaryButtonHeight, label.implicitHeight + 2 * Theme.space.sectionGap)
    activeFocusOnTab: true
    opacity: ownDisabled ? 0.4 : 1

    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Rectangle {
        objectName: "buttonBackground"
        anchors.fill: parent
        radius: root.compact ? Theme.radius.field : Theme.radius.primaryButton
        color: {
            if (root.variant === PrimaryButton.Primary)
                return root.pressed ? Theme.colour.primaryPressed : Theme.colour.primary;
            if (root.variant === PrimaryButton.Secondary || root.pressed)
                return Theme.colour.primaryTint;
            return "transparent";
        }
        border.width: root.activeFocus ? Theme.border.otherWidth : 0
        border.color: Theme.colour.primaryText
    }

    Row {
        id: content

        anchors.centerIn: parent
        spacing: root.hasIcon ? Theme.space.tileGap : 0

        Icon {
            objectName: "buttonIcon"
            visible: root.hasIcon
            anchors.verticalCenter: parent.verticalCenter
            kind: root.iconKind
            size: root.iconSize
            colour: root.labelColour
        }

        Text {
            id: label

            objectName: "buttonLabel"
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(implicitWidth, root.width - 2 * Theme.space.rowPadding - (root.hasIcon ? root.iconSize + content.spacing : 0))
            horizontalAlignment: Text.AlignHCenter
            color: root.labelColour
            font: root.compact ? Theme.font.buttonCompact.font : Theme.font.sheetTitle.font
            text: root.text
            wrapMode: Text.WordWrap
            Accessible.ignored: true
        }
    }

    TapHandler {
        id: tap

        onTapped: root.clicked()
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
