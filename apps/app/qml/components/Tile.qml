// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// One tile of the capture board (Stage 4, package 4.5; docs/plan/stage-4.md, "Tile"). It shows what a row of BoardModel
// holds and tells the screen about the finger; it counts nothing and records nothing.
//
// The roles of a BoardModel row arrive as required properties, so a Repeater over the model fills them by name.
//
//   pressed()  the finger went down. The screen fires the impact haptic and then records the entry, from this handler
//              and nothing else (a finger that slides off cannot fire a haptic without an entry, Stage 5 decision 1).
//   tapped()   the finger came up inside the tile. A release alone, or a release after sliding off, emits neither.
//
// A VoiceOver activation has no touch-down, so it sends the same pair, pressed() and then tapped() (decision 7), and the
// screen's two calls are the same however the tile was reached.
//
// The press scale runs on the render thread (ScaleAnimator), so it answers the finger at once and keeps going while the
// GUI thread is busy with the commit. It reverses on release and on cancel. An empty position and a skeleton draw a
// shape and take no touch; the skeleton draws neutral shapes only, with no text, count or type colour (the launch screen).
//
// Look: 12 pt padding (10 pt above the default text size, so four lines fit at the 1.3 cap), the label 13.5/600 with a
// line of 1.2 times its size, the card shadow (Theme.shadow.card, a plain offset rectangle, so the press costs nothing
// more), the count badge 24 pt with 7 pt side padding and a number that does not grow past the default size, and on
// Other the 24 pt search icon in the top left corner.
Item {
    id: root

    required property string typeId
    required property string label
    required property int colourSlot
    required property int count
    required property bool hasCount
    required property bool isOther
    required property bool isEmpty
    required property string accessibleName
    property bool skeleton: false

    signal pressed
    signal tapped

    // The tile takes touch and is announced as a button. Empty positions and the skeleton are neither.
    readonly property bool interactive: !isEmpty && !skeleton
    // A finger is down on the tile.
    readonly property bool down: tap.pressed
    // The largest number the badge writes in full; from here on it reads "99+" (the accessible name has the full count).
    readonly property int badgeLimit: 99
    readonly property real padding: Theme.font.tileLabel.enlarged ? Theme.space.sectionGap - Theme.border.hairline * 2 : Theme.space.sectionGap
    // The floor of the label's fit (fontSizeMode), as a share of its size: the label wraps and shrinks to this, never elides.
    readonly property real labelFloor: 0.75
    readonly property bool dashed: isOther && !skeleton
    readonly property color fillColour: skeleton ? Theme.colour.swatch : (isOther ? Theme.colour.otherTileFill : Theme.tileColour(colourSlot))

    implicitWidth: Theme.size.tileWidth
    implicitHeight: Theme.size.tileHeight

    Accessible.role: root.interactive ? Accessible.Button : Accessible.NoRole
    Accessible.name: root.accessibleName
    Accessible.description: root.isOther ? qsTr("Opens the list of error types") : ""
    Accessible.ignored: !root.interactive
    Accessible.focusable: root.interactive
    Accessible.onPressAction: {
        if (root.interactive) {
            root.pressed();
            root.tapped();
        }
    }

    // Everything that is drawn is in `face`, so the press scales the picture and not the touch area.
    Item {
        id: face

        objectName: "tileFace"
        anchors.fill: parent

        // An empty position: an outline of the slot and nothing in it.
        Rectangle {
            objectName: "tileEmptySlot"
            anchors.fill: parent
            visible: root.isEmpty && !root.skeleton
            radius: Theme.radius.tile
            color: "transparent"
            border.width: Theme.border.hairline
            border.color: Theme.colour.border
        }

        // The card shadow of a tile with a type: a plain rectangle a point lower, under the fill.
        Rectangle {
            objectName: "tileShadow"
            width: parent.width
            height: parent.height
            y: Theme.shadow.card.offsetY
            visible: !root.isEmpty && !root.dashed && !root.skeleton
            radius: Theme.radius.tile
            color: Theme.shadow.card.colour
        }

        // A tile with a type, and the skeleton.
        Rectangle {
            objectName: "tileBackground"
            anchors.fill: parent
            visible: !root.isEmpty && !root.dashed
            radius: Theme.radius.tile
            color: root.fillColour
        }

        // Other: a neutral fill and a dashed border.
        Shape {
            objectName: "tileOtherBorder"
            anchors.fill: parent
            visible: root.dashed
            preferredRendererType: Shape.CurveRenderer

            ShapePath {
                objectName: "tileOtherPath"
                fillColor: root.fillColour
                strokeColor: Theme.colour.otherTileBorder
                strokeWidth: Theme.border.otherWidth
                strokeStyle: ShapePath.DashLine
                dashPattern: [3, 2]
                capStyle: ShapePath.FlatCap

                // Inside the tile, so the stroke is not cut off at the edge.
                PathRectangle {
                    x: Theme.border.otherWidth / 2
                    y: Theme.border.otherWidth / 2
                    width: root.width - Theme.border.otherWidth
                    height: root.height - Theme.border.otherWidth
                    radius: Theme.radius.tile
                }
            }
        }

        // The search icon of Other.
        Icon {
            objectName: "tileOtherIcon"
            x: root.padding
            y: root.padding
            visible: root.dashed
            kind: "search"
            colour: Theme.colour.primaryText
        }

        Text {
            id: labelText

            objectName: "tileLabel"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.padding
            anchors.rightMargin: root.padding
            anchors.bottomMargin: root.padding
            // The label is bottom-aligned and may use the whole height above its margin: four lines at the 1.3 cap are
            // 86 pt, which a 104 pt tile holds only when the top margin is not taken from them.
            anchors.topMargin: 0
            visible: !root.isEmpty && !root.skeleton
            text: root.label
            color: Theme.colour.tileText
            font: Theme.font.tileLabel.font
            // A line is 1.2 times the size, as CSS has it (16.2 pt at 13.5), not 1.2 times the font's own line. It is
            // given as a ratio of the font's line, because the label shrinks to fit (fontSizeMode) and a ratio shrinks
            // with it.
            lineHeight: Theme.font.tileLabel.lineRatio
            lineHeightMode: Text.ProportionalHeight
            // Wraps at words, and inside a word that is wider than the tile; never elides (NFR-02, decision 9).
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignBottom
            // The tile never grows, so a label that would not fit shrinks, down to a floor, before it is cut.
            fontSizeMode: Text.Fit
            minimumPixelSize: Math.round(Theme.font.tileLabel.size * root.labelFloor)
            // A fractional size is a point size, and Fit then shrinks to the point floor.
            minimumPointSize: Theme.font.tileLabel.size * root.labelFloor * 72 / Theme.fontDpi
            Accessible.ignored: true
        }

        Rectangle {
            id: badge

            objectName: "tileBadge"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.space.tileGap
            visible: root.hasCount && !root.skeleton && !root.isEmpty
            width: Math.max(Theme.size.badge, badgeText.implicitWidth + (Theme.space.tileGap - Theme.border.hairline) * 2)
            height: Theme.size.badge
            radius: height / 2
            color: Theme.colour.badgeBackground
            Accessible.ignored: true

            Text {
                id: badgeText

                objectName: "tileBadgeText"
                anchors.centerIn: parent
                text: root.count >= root.badgeLimit ? qsTr("%1+").arg(root.badgeLimit) : root.count
                color: Theme.colour.badgeText
                font: Theme.font.badge.font
                Accessible.ignored: true
            }
        }
    }

    ScaleAnimator {
        id: pressIn

        objectName: "tilePressIn"
        target: face
        to: Theme.motion.tilePressScale
        duration: Theme.motion.tilePressMs
        easing.type: Theme.motion.easingPress
    }

    ScaleAnimator {
        id: pressOut

        objectName: "tilePressOut"
        target: face
        to: 1
        duration: Theme.motion.tilePressMs
        easing.type: Theme.motion.easingPress
    }

    TapHandler {
        id: tap

        objectName: "tileTap"
        enabled: root.interactive
        // The tap is cancelled when the finger leaves the tile, so sliding off neither taps nor leaves the tile pressed.
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (pressed) {
                // First, before anything that draws: the screen's haptic and record are in this signal.
                root.pressed();
                pressOut.stop();
                pressIn.restart();
            } else {
                pressIn.stop();
                pressOut.restart();
            }
        }
        onTapped: root.tapped()
    }
}
