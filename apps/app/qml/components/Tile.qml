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
// A tile with an error type is logged by a double-tap or by a hold (4.17a; BRD-02, BRD-07), never by one tap, which is too easy to
// make by accident. The tile tells the screen when its touch has asked for an entry, and the screen fires the haptic and records
// it from that handler and nothing else:
//
//   pressed()  this touch asks for the entry: the finger went down for the second tap of a double-tap (two taps, the lift of the
//              first and the touch of the second no more than `doubleTapMs` apart), or it has stayed down for `holdMs` and
//              nothing has been asked yet. The screen fires the impact haptic and then records the entry, from this handler and
//              nothing else (a finger that slides off cannot fire a haptic without an entry, Stage 5 decision 1). The haptic and the
//              press scale come with this signal, so they are at the second tap and not at the first.
//   held()     the finger stayed down on the tile for `holdMs` (500 ms) without leaving it (BRD-07). It comes after pressed(): the
//              hold asked for the entry itself, or the second tap of a double-tap that went on has asked already, and either way
//              the entry is recorded once. Sliding off, letting go earlier and a cancelled touch send nothing.
//   singleTapped()  one tap went by and no second came within `doubleTapMs`: it logged nothing, and the screen shows the hint
//              "Double-tap to log". A tap that is part of a double-tap, a triple-tap and so on sends no hint.
//   tapped()   the finger came up inside the tile. The Other tile uses it to open the list of error types.
//
// Taps are counted for each tile on their own, so a tap on another tile in between is not the first half of anything. The count
// cycles: the second tap logs, the third is part of the same gesture and logs nothing, and the fourth starts a new double-tap.
// A hold, a slide off and a cancelled touch end the count. The window and the hold run on the animation clock (a NumberAnimation
// of a property that nothing reads, then a ScriptAction), not a Timer, so that a test moves them with the rig's `advance`.
//
// The Other tile is a navigation, not a log: it opens the list on a single tap, sends pressed() at the touch (the screen records
// nothing for it) and tapped() at the lift, and has no double-tap, no hold and no hint.
//
// A VoiceOver activation is already a double-tap of the screen reader, so it needs no second: it has no touch-down and sends
// pressed() and then tapped() (decision 7), and the screen's two calls are the same however the tile was reached. The board has a
// second VoiceOver element for each tile that can be held, "Log <name> with note", while a note is typed (BoardGrid): it sends
// pressed() and then held() for a person who cannot make the hold.
//
// An empty position is a button too, with a plus in it, so that a new board shows where its types go: it sends tapped()
// alone, never pressed(), because there is nothing to record. The screen takes the person to the board setup.
//
// The press scale runs on the render thread (ScaleAnimator), so it answers the finger at once and keeps going while the
// GUI thread is busy with the commit. It reverses on release and on cancel. The skeleton draws a shape and takes no
// touch; it draws neutral shapes only, with no text, count or type colour (the launch screen).
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
    // Under the empty-board card of the Log screen: drawn, but out of the screen reader's way (the card is read instead).
    property bool covered: false
    // How long the finger has to stay down for held() (BRD-07, docs/plan/stage-4.md, 4.17), and the longest gap between the lift of
    // a tap and the touch of the next for a double-tap (4.17a). Both are Theme.motion values and both are timed on the animation
    // clock, like the press scale, so that a test moves them with the rig's `advance`.
    property int holdMs: Theme.motion.holdMs
    property int doubleTapMs: Theme.motion.doubleTapMs
    // Runs from 0 to 1 while a hold or the window of a double-tap is timed (see `holdTimer` and `tapWindow`); nothing reads them.
    property real holdClock: 0
    property real windowClock: 0
    // The taps counted so far in the gesture that is open (1 to 3), 0 when none is. The window is open between the lift of a tap
    // and the end of `doubleTapMs`.
    property int tapCount: 0
    // The entry has been asked for by this touch (pressed() was sent), so the hold does not ask a second time.
    property bool askedByTouch: false
    // The touch has gone on into a hold (held() was sent): its lift is not a tap.
    property bool holdFired: false
    // The touch was cancelled (a slide off, or the system took it): its lift is not a tap either.
    property bool touchLost: false
    signal pressed
    signal tapped
    signal held
    signal singleTapped

    // The tile takes touch and is announced as a button. Empty positions and the skeleton are neither.
    readonly property bool interactive: !isEmpty && !skeleton
    // An empty position: a button that only taps (it records nothing).
    readonly property bool addable: isEmpty && !skeleton
    // A touch on this tile counts towards a double-tap and can go on into a hold. Other opens a list on its single tap and has
    // neither.
    readonly property bool holdable: interactive && !isOther
    // A finger is down on the tile.
    readonly property bool down: tap.pressed
    // The largest number the badge writes in full; from here on it reads "99+" (the accessible name has the full count).
    readonly property int badgeLimit: 99
    readonly property real padding: WindowLayout.tileLabelRole.enlarged ? Theme.space.sectionGap - Theme.border.hairline * 2 : Theme.space.sectionGap
    // The floor of the label's fit (fontSizeMode), as a share of its size: the label wraps and shrinks to this, never elides.
    readonly property real labelFloor: 0.75
    // The longest word of the label, as written. A word is never broken, so the label is first sized to give it a line.
    readonly property string longestWord: {
        let longest = "";
        for (const word of label.split(" ")) {
            if (word.length > longest.length)
                longest = word;
        }
        return longest;
    }
    // The size, as a share of the role's, at which the longest word fills a line: 1 for a word that fits, as every short
    // label does, so those tiles keep the type scale's size. It goes no lower than 0.75 of the role's size without the
    // text scale (the floor of 10 pt at the default size), which holds at the larger sizes, where a word needs less of it.
    readonly property real wordFitFloor: Math.min(1, labelFloor * WindowLayout.tileLabelRole.basePt / WindowLayout.tileLabelRole.size)
    readonly property real wordFit: wordProbe.implicitWidth > 0 ? Math.max(wordFitFloor, Math.min(1, labelText.width / wordProbe.implicitWidth)) : 1
    // A word that is wider than the tile even at that floor is the last resort: it is broken, and not cut off.
    readonly property bool wordFits: wordProbe.implicitWidth * wordFitFloor <= labelText.width + 0.5
    readonly property bool dashed: isOther && !skeleton
    readonly property color fillColour: skeleton ? Theme.colour.swatch : (isOther ? Theme.colour.otherTileFill : Theme.tileColour(colourSlot))

    implicitWidth: Theme.size.tileWidth
    implicitHeight: Theme.size.tileHeight

    Accessible.role: root.interactive || root.addable ? Accessible.Button : Accessible.NoRole
    Accessible.name: root.accessibleName
    Accessible.description: root.isOther ? qsTr("Opens the list of error types") : root.addable ? qsTr("Opens the board setup in Settings") : ""
    Accessible.ignored: root.covered || (!root.interactive && !root.addable)
    Accessible.focusable: !root.covered && (root.interactive || root.addable)
    Accessible.onPressAction: {
        if (root.interactive) {
            root.pressed();
            root.tapped();
        } else if (root.addable) {
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

        // The plus of an empty position: where a type can go.
        Icon {
            objectName: "tileAddIcon"
            anchors.centerIn: parent
            visible: root.addable
            size: Theme.size.badge
            kind: "plus"
            colour: Theme.colour.textSecondary
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

        // The longest word at the role's own size, to measure it.
        Text {
            id: wordProbe

            objectName: "tileWordProbe"
            visible: false
            text: root.longestWord
            font: WindowLayout.tileLabelRole.font
            wrapMode: Text.NoWrap
            Accessible.ignored: true
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
            font: {
                const sized = WindowLayout.tileLabelRole.font;
                // A whole size is a pixel size and a fractional one a point size (Theme, FontRole.make).
                if (root.wordFit < 1) {
                    if (sized.pixelSize > 0)
                        sized.pixelSize = Math.floor(sized.pixelSize * root.wordFit);
                    else
                        sized.pointSize = sized.pointSize * root.wordFit;
                }
                return sized;
            }
            // A line is 1.2 times the size, as CSS has it (16.2 pt at 13.5), not 1.2 times the font's own line. It is
            // given as a ratio of the font's line, because the label shrinks to fit (fontSizeMode) and a ratio shrinks
            // with it.
            lineHeight: WindowLayout.tileLabelRole.lineRatio
            lineHeightMode: Text.ProportionalHeight
            // Wraps at words only, as the longest word has been sized to a line; inside a word only when it is wider than the
            // tile even at the smallest size. It never elides (NFR-02, decision 9).
            wrapMode: root.wordFits ? Text.WordWrap : Text.Wrap
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignBottom
            // The tile never grows, so a label that would not fit shrinks, down to a floor, before it is cut.
            fontSizeMode: Text.Fit
            minimumPixelSize: Math.round(WindowLayout.tileLabelRole.size * Math.min(root.labelFloor, root.wordFit))
            // A fractional size is a point size, and Fit then shrinks to the point floor.
            minimumPointSize: WindowLayout.tileLabelRole.size * Math.min(root.labelFloor, root.wordFit) * 72 / Theme.fontDpi
            Accessible.ignored: true
        }

        Rectangle {
            id: badge

            objectName: "tileBadge"
            anchors.top: parent.top
            anchors.right: parent.right
            anchors.margins: Theme.space.tileGap
            visible: root.hasCount && !root.skeleton && !root.isEmpty
            width: Math.max(WindowLayout.badge, badgeText.implicitWidth + (Theme.space.tileGap - Theme.border.hairline) * 2)
            height: WindowLayout.badge
            radius: height / 2
            color: Theme.colour.badgeBackground
            Accessible.ignored: true

            Text {
                id: badgeText

                objectName: "tileBadgeText"
                anchors.centerIn: parent
                text: root.count >= root.badgeLimit ? qsTr("%1+").arg(root.badgeLimit) : root.count
                color: Theme.colour.badgeText
                font: WindowLayout.badgeRole.font
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

    // The hold of BRD-07: runs from the touch and is stopped by the lift, a slide off and a cancel. It is timed on the animation
    // clock and not by a Timer, so that the rig's virtual time moves it (a Timer runs on the wall clock). The clock is a number
    // that runs from 0 to 1 and nothing reads: Qt ticks a lone PauseAnimation by the wall clock, and only a property animation
    // by the animation clock that the rig replaces.
    SequentialAnimation {
        id: holdTimer

        objectName: "tileHold"

        NumberAnimation {
            target: root
            property: "holdClock"
            from: 0
            to: 1
            duration: root.holdMs
        }

        ScriptAction {
            script: root.completeHold()
        }
    }

    // The window of a double-tap: opens at the lift of a tap, and the next touch inside it is the next tap of the gesture. When it
    // ends with one tap counted, that tap was alone.
    SequentialAnimation {
        id: tapWindow

        objectName: "tileTapWindow"

        NumberAnimation {
            target: root
            property: "windowClock"
            from: 0
            to: 1
            duration: root.doubleTapMs
        }

        ScriptAction {
            script: root.closeWindow()
        }
    }

    // The touch asks for the entry: the screen's haptic and record are in `pressed`, then the press scale. First, before anything
    // that draws.
    function ask(): void {
        askedByTouch = true;
        root.pressed();
        pressOut.stop();
        pressIn.restart();
    }

    function completeHold(): void {
        // A hold that no tap has asked for asks now (it logs at the hold, not at the touch), and the hold comes after it.
        if (!askedByTouch)
            ask();
        holdFired = true;
        tapCount = 0;
        root.held();
    }

    function closeWindow(): void {
        const alone = tapCount === 1;
        tapCount = 0;
        if (alone)
            root.singleTapped();
    }

    // The finger went down on a tile that logs. The touch is the next tap of the open gesture, or the first of a new one; the
    // second tap of a gesture logs, the third does nothing more, and the fourth starts again.
    function touchDown(): void {
        askedByTouch = false;
        holdFired = false;
        touchLost = false;
        const counting = tapWindow.running;
        tapWindow.stop();
        tapCount = counting && tapCount < 3 ? tapCount + 1 : 1;
        if (tapCount === 2)
            ask();
        holdTimer.restart();
    }

    function touchUp(): void {
        holdTimer.stop();
        pressIn.stop();
        pressOut.restart();
        // A hold is not a tap and neither is a touch that was lost: they have ended the gesture. Anything else opens the window
        // for the next tap.
        if (!holdFired && !touchLost)
            tapWindow.restart();
    }

    // A slide off or a cancelled touch ends the gesture.
    function touchCancelled(): void {
        touchLost = true;
        holdTimer.stop();
        tapWindow.stop();
        tapCount = 0;
    }

    TapHandler {
        id: tap

        objectName: "tileTap"
        // A covered tile is under a card that takes its touches; it has none of its own.
        enabled: !root.covered && (root.interactive || root.addable)
        // The tap is cancelled when the finger leaves the tile, so sliding off neither taps nor leaves the tile pressed.
        gesturePolicy: TapHandler.WithinBounds
        onPressedChanged: {
            if (root.addable)
                return;
            if (root.holdable) {
                if (pressed)
                    root.touchDown();
                else
                    root.touchUp();
            } else if (pressed) {
                // Other: the touch is the press, and the screen records nothing for it.
                root.pressed();
                pressOut.stop();
                pressIn.restart();
            } else {
                pressIn.stop();
                pressOut.restart();
            }
        }
        onCanceled: root.touchCancelled()
        onTapped: root.tapped()
    }
}
