// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Effects

// The blurred shadow of a rounded card, one of Theme.shadow: put it first among the card's siblings, with the card's
// geometry, and the card is drawn over it.
//
//     Shadow { anchors.fill: card; radius: card.radius; token: Theme.shadow.toast }
//
// It blurs a hidden rounded rectangle in the shadow colour with a MultiEffect and moves it down by the token's offset.
// It is for the single cards that float: the toast, a floating banner, an alert, a sheet and the lifted tile. The card
// shadow of the tiles, the note card and the "Last" bar (Theme.shadow.card, 0 1 2) is too small to need a blur: those
// draw it as a plain rounded rectangle offset by a point, so the twelve tiles cost one item each and no effect.
//
// This file is a part of the components that use it and is tested through them.
Item {
    id: root

    // One of Theme.shadow (toast, alert, sheet, lift).
    required property var token
    property real radius: 0
    // The corners of a bottom sheet: only the top ones are round.
    property bool topCornersOnly: false

    Accessible.ignored: true

    // Made only while the shadow is shown, so a hidden toast or banner costs one item and no effect.
    Loader {
        objectName: "shadowLoader"
        anchors.fill: parent
        active: root.visible

        sourceComponent: Item {
            Rectangle {
                id: shape

                visible: false
                width: parent.width
                height: parent.height
                topLeftRadius: root.radius
                topRightRadius: root.radius
                bottomLeftRadius: root.topCornersOnly ? 0 : root.radius
                bottomRightRadius: root.topCornersOnly ? 0 : root.radius
                color: root.token.colour
            }

            MultiEffect {
                objectName: "shadowEffect"
                source: shape
                width: parent.width
                height: parent.height
                y: root.token.offsetY
                autoPaddingEnabled: true
                blurEnabled: true
                blur: 1
                blurMax: Math.round(root.token.blur)
            }
        }
    }
}
