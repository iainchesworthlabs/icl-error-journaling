// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// A modal surface over the window (Stage 4, package 4.4): the bottom sheet of the Other picker, the version change and the
// edit entry, and, with `presentation: Sheet.Centre`, the card that Alert is made of. It is built from plain items and
// not from a Popup, so that its whole tree sits under the item that holds it and the tests of the rig see every control in it.
//
// A sheet fills its parent, which is the window or the SheetHost's sheet layer. Put its content inside it: the content
// area is the sheet below the grabber and above the home indicator (or above the keyboard), and the content fills it.
//
//   scrim      dims the window, takes every touch that is not on the sheet and, when `dismissible`, dismisses on a tap.
//              It is a barrier and not a control: a screen reader reaches "Close" on the grabber instead.
//   grabber    the 36 x 5 pill and a 44 pt strip under it. Dragging it down past `dismissDistance` dismisses the sheet;
//              a shorter drag lets go and the sheet springs back. For a screen reader it is a button named "Close".
//   detents    Medium is half the window. Large starts at Theme.size.sheetTop (54 pt on the design frame, 5 pt above the
//              59 pt safe area; on another phone the same distance from its inset). Content is as tall as the content
//              needs (`contentImplicitHeight`, plus the grabber and the bottom inset): the template value sheet over the
//              keyboard, and the time picker, which is a second, shorter sheet over Edit entry or the version sheet
//              (declare it after that sheet in the same SheetHost and it stacks over it, with its own scrim).
//   bottom     a bottom sheet runs to the bottom edge of the window, over the tab bar. By default its content area ends
//              above the home indicator. With `contentUnderHomeIndicator` the content area runs to the bottom edge too,
//              and the content pads itself by `contentBottomInset`, so a list scrolls under the indicator instead of
//              being cut 34 pt above it.
//   shadow     a bottom sheet casts Theme.shadow.sheet upwards; an alert card casts Theme.shadow.alert.
//   centre     the card of an alert hangs from Theme.size.alertAnchor (374 pt on the design frame): the point 3/8 of the way
//              down the card is there, so a taller card sits higher, as in the mock-ups, and none is centred; it moves up
//              when it is too tall to fit below that, never into the top inset.
//   form       in a window that is not Compact (an iPad, Stage 8) a bottom sheet is a form sheet: a card as wide as
//              Theme.layout.sheetWidth in the middle of the window, as tall as its content (the Content detent) or as
//              the largest it may be (the others), never nearer the window's edge than Theme.layout.sheetMargin, with
//              all four corners rounded, no grabber and no drag. It comes in and goes as an alert does, by fading and
//              scaling in place. An alert in such a window is the same card, centred vertically. A Compact window,
//              which is every phone, keeps the bottom sheet and the alert as drawn above.
//   keyboard   `keyboardHeight` lifts the sheet's bottom edge to the top of the keyboard; the sheet gets shorter if it
//              would otherwise reach into the top inset, and the content area ends at the keyboard.
//   focus      opening moves focus to the sheet; closing returns focus to the item that had it. What keeps Tab inside is
//              SheetHost, which disables the screen under its sheets: a sheet outside a host does not trap it.
//   motion     the sheet slides up on a YAnimator, the scrim fades, both on Theme.motion. With Reduce Motion nothing
//              slides: the sheet fades in place in 100 ms.
//
// The sheet closes itself; its owner learns of it from `closed`. Open it with `open()`.
FocusScope {
    id: root

    enum Presentation {
        Bottom,
        Centre
    }

    enum Detent {
        Medium,
        Large,
        Content
    }

    // What a screen reader says for the sheet, such as "Log another error".
    required property string accessibleName
    property string accessibleDescription
    property int presentation: Sheet.Bottom
    property int detent: Sheet.Large
    // Whether a drag of the grabber, a tap on the scrim or the Escape key dismisses it.
    property bool dismissible: true
    property color scrimColour: Theme.colour.sheetScrim
    // How far the keyboard reaches up into the sheet's layer, in points; 0 when there is none. It is read from Keyboard
    // (assumption U5), in the coordinates of the window: the layer is the window or a screen, and the keyboard docks at the
    // bottom of the window, so a layer that ends above the window's bottom (above the tab bar) is covered by less than the
    // height of the keyboard. A test can set it directly.
    property real keyboardHeight: Keyboard.coverage(root)
    // For a centred sheet and the Content detent: the height its content needs, so that the sheet is no taller than that.
    property real contentImplicitHeight: 0
    // The content area of a bottom sheet runs under the home indicator; the content pads itself by contentBottomInset.
    property bool contentUnderHomeIndicator: false
    // How far the grabber has to be dragged down to dismiss.
    property real dismissDistance: Theme.size.tileHeight

    // The safe-area insets. SafeFrame reads them; a test overrides them.
    property alias overrideInsets: frame.overrideInsets
    property alias overrideTop: frame.overrideTop
    property alias overrideBottom: frame.overrideBottom
    property alias overrideLeft: frame.overrideLeft
    property alias overrideRight: frame.overrideRight
    readonly property real topInset: frame.topInset
    readonly property real bottomInset: frame.bottomInset

    default property alias content: body.data

    // True from open() until the dismissal has finished.
    readonly property bool isOpen: modalState.open
    // The panel and the content area, for a test and for Alert.
    readonly property Item panelItem: panel
    readonly property Item contentArea: body

    // The presentation finished.
    signal opened
    // The dismissal finished, once for each time the sheet was open.
    signal closed

    // ---- geometry ----

    readonly property bool centred: presentation === Sheet.Centre
    // A bottom sheet in a window that is not Compact is a card in the middle of it (see "form" above).
    readonly property bool formSheet: !centred && !WindowLayout.compact
    // Whether the sheet comes in and goes by fading and scaling in place, and not by sliding.
    readonly property bool popIn: centred || formSheet
    readonly property real cardMargin: 2 * Theme.space.screenMargin
    // The top of a large sheet: Theme.size.sheetTop on the design frame, the same distance from another phone's inset.
    readonly property real largeTop: Math.max(0, topInset + Theme.size.sheetTop - Theme.frame.safeAreaTop)
    readonly property real largeHeight: root.height - largeTop
    // The padding the content keeps at the bottom when it runs under the home indicator: the inset, or 0 over a keyboard.
    // A form sheet is clear of the home indicator, so its content has nothing to pad for.
    readonly property real contentBottomInset: contentUnderHomeIndicator && keyboardHeight <= 0 && !formSheet ? bottomInset : 0
    // The part of a bottom sheet under its content area: the home indicator, unless the content runs under it; a form
    // sheet keeps a screen margin.
    readonly property real bottomReserve: formSheet ? Theme.space.screenMargin : keyboardHeight > 0 || contentUnderHomeIndicator ? 0 : bottomInset
    readonly property real mediumHeight: root.height / 2
    // The bottom edge of what the sheet may use: the window, or the top of the keyboard.
    readonly property real bottomEdge: root.height - keyboardHeight
    // The space the content may use above that edge.
    readonly property real available: bottomEdge - (centred ? topInset : largeTop)
    // The same for a centred card, which stays clear of the home indicator when there is no keyboard.
    readonly property real centreAvailable: available - (keyboardHeight > 0 ? 0 : bottomInset)
    readonly property real bodyPadding: Theme.space.screenMargin + Theme.space.tileGap / 2
    // The grabber: 8 pt above it and the 5 pt pill. The bar of the sheet (NavBar `inSheet`) has the 8 pt under it, so the
    // row of its actions starts 21 pt under the top of the sheet, as the mock-ups have it.
    readonly property real grabberZone: Theme.space.tileGap + Theme.border.hairline * 5
    // The height of the Content detent: the grabber, the content and what is kept under it.
    readonly property real contentHeight: grabberZone + contentImplicitHeight + (keyboardHeight > 0 ? 0 : bottomInset)
    // The form sheet: its width, the space it may use (clear of the insets, the keyboard and the margin), and its height,
    // which is its content's under the Content detent and else the most it may be.
    readonly property real formWidth: Math.min(Theme.layout.sheetWidth, root.width - 2 * Theme.layout.screenMargin)
    readonly property real formTop: topInset + Theme.layout.sheetMargin
    readonly property real formBottom: bottomEdge - (keyboardHeight > 0 ? Theme.space.screenMargin : bottomInset + Theme.layout.sheetMargin)
    readonly property real formSpace: Math.max(0, formBottom - formTop)
    readonly property real formNatural: detent === Sheet.Content ? grabberZone + contentImplicitHeight + Theme.space.screenMargin : Theme.layout.sheetMaxHeight
    readonly property real formHeight: Math.min(formNatural, Theme.layout.sheetMaxHeight, formSpace)
    // The width of the card of an alert: the design frame's less its margins on every window, so it is not stretched.
    readonly property real alertWidth: WindowLayout.compact ? root.width - 2 * cardMargin : Theme.frame.width - 2 * cardMargin
    readonly property real panelHeight: formSheet ? formHeight : centred ? Math.min(contentImplicitHeight + Theme.size.badge + Theme.space.screenMargin, centreAvailable - 2 * Theme.space.screenMargin) : Math.min(detent === Sheet.Medium ? mediumHeight : detent === Sheet.Content ? contentHeight : largeHeight, available)
    // The card of an alert: 3/8 of its height above alertAnchor on the design frame (the same distance from another phone's
    // inset), moved up when the card would not fit under that, and never above the inset.
    readonly property real alertTop: topInset + Theme.size.alertAnchor - Theme.frame.safeAreaTop - panelHeight * 3 / 8
    // In a window that is not Compact the card is centred in the space between the inset and the bottom edge instead.
    readonly property real alertCentredTop: topInset + Math.round((centreAvailable - panelHeight) / 2)
    readonly property real centreTop: Math.max(topInset + Theme.space.screenMargin, Math.min(WindowLayout.compact ? alertTop : alertCentredTop, topInset + centreAvailable - Theme.space.screenMargin - panelHeight))
    readonly property real restY: formSheet ? formTop + Math.round((formSpace - formHeight) / 2) : centred ? centreTop : bottomEdge - panelHeight
    // The level the scrim reaches. It is a fraction of the scrim colour's own opacity, not a design colour.
    readonly property real scrimLevel: 0.6

    property real dragOffset: 0
    property real snapFrom: 0

    anchors.fill: parent
    visible: modalState.open

    QtObject {
        id: modalState

        property bool open: false
        property bool closing: false
        // The item that had focus when the sheet opened, to give it back when the sheet closes. It is never a text field: a
        // field is left when a sheet opens (its value is saved by its own screen) and must not bring the keyboard back
        // when the sheet goes.
        property Item previousFocus: null
    }

    SafeFrame {
        id: frame

        anchors.fill: parent
    }

    // ---- opening and closing ----

    function open(): void {
        if (modalState.open && !modalState.closing)
            return;
        dismissAnimation.stop();
        if (!modalState.open) {
            const window = Window.window;
            const focused = window ? window.activeFocusItem : null;
            modalState.previousFocus = Keyboard.isTextInput(focused) ? null : focused;
            // The field that was being typed in is left now: it saves what it holds on losing the focus, and the keyboard goes.
            if (Keyboard.isTextInput(focused))
                Keyboard.dismiss(root, null);
        }
        modalState.closing = false;
        modalState.open = true;
        root.dragOffset = 0;
        root.bindGeometry();
        panel.forceActiveFocus();
        presentAnimation.restart();
    }

    function close(): void {
        if (!modalState.open || modalState.closing)
            return;
        modalState.closing = true;
        // The keyboard goes with the sheet, not after its animation.
        Keyboard.dismiss(root, null);
        presentAnimation.stop();
        snapBack.stop();
        dismissAnimation.restart();
    }

    // An animator writes its end value to the item when it finishes, and that replaces a binding. The position of the
    // panel and the level of the scrim are bound to the geometry and to the drag, so they are bound again after each.
    function bindGeometry(): void {
        panel.y = Qt.binding(() => root.restY + root.dragOffset);
        scrim.opacity = Qt.binding(() => root.scrimLevel * (1 - Math.min(1, root.dragOffset / Math.max(1, root.panelHeight))));
    }

    function finishClose(): void {
        modalState.open = false;
        modalState.closing = false;
        root.dragOffset = 0;
        root.bindGeometry();
        const target = modalState.previousFocus;
        modalState.previousFocus = null;
        if (target && !Keyboard.isTextInput(target))
            target.forceActiveFocus(Qt.OtherFocusReason);
        root.closed();
    }

    function releaseDrag(): void {
        if (!modalState.open || modalState.closing)
            return;
        if (root.dragOffset > root.dismissDistance) {
            root.close();
        } else if (root.dragOffset > 0) {
            root.snapFrom = panel.y;
            root.dragOffset = 0;
            snapBack.restart();
        }
    }

    // ---- the scrim ----

    Rectangle {
        id: scrim

        objectName: "sheetScrim"
        anchors.fill: parent
        color: root.scrimColour
        opacity: 0
        Accessible.ignored: true

        MultiPointTouchArea {
            anchors.fill: parent
            mouseEnabled: true
            onReleased: {
                if (root.dismissible && modalState.open)
                    root.close();
            }
        }
    }

    // ---- the sheet ----

    Rectangle {
        id: panel

        objectName: "sheetPanel"
        x: root.popIn ? (root.width - width) / 2 : 0
        y: root.restY + root.dragOffset
        width: root.formSheet ? root.formWidth : root.centred ? root.alertWidth : root.width
        // The bottom corners of a bottom sheet are rounded too; they are drawn below the window. A form sheet is a card.
        height: root.panelHeight + (root.popIn ? 0 : Theme.radius.sheetTop)
        radius: root.centred ? Theme.radius.alert : Theme.radius.sheetTop
        color: root.centred ? Theme.colour.surfaceRaised : Theme.colour.sheetBackground
        transformOrigin: Item.Center
        focus: true
        activeFocusOnTab: true
        Accessible.role: Accessible.Dialog
        Accessible.name: root.accessibleName
        Accessible.description: root.accessibleDescription
        Accessible.focusable: true
        Keys.onEscapePressed: event => {
            if (root.dismissible && modalState.open)
                root.close();
            event.accepted = true;
        }

        // The shadow, under the panel (a child with a negative z is drawn below its parent), so it moves, scales and fades
        // with it.
        Shadow {
            z: -1
            width: parent.width
            height: parent.height
            radius: parent.radius
            topCornersOnly: !root.popIn
            token: root.popIn ? Theme.shadow.alert : Theme.shadow.sheet
        }

        // Takes every touch that lands on the sheet outside its controls, so none reaches the scrim.
        MultiPointTouchArea {
            anchors.fill: parent
            mouseEnabled: true
        }

        Item {
            id: grabber

            objectName: "sheetGrabber"
            visible: !root.popIn
            // A large sheet starts a little above the top inset, as the design has it; the strip that takes the drag
            // starts at the inset, so nothing that takes a touch is in it. The pill is drawn where the design has it.
            y: Math.max(0, root.topInset - root.restY)
            width: parent.width
            height: Theme.size.minTouchTarget
            Accessible.ignored: root.popIn || !root.dismissible
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Close")
            Accessible.onPressAction: root.close()

            DragHandler {
                target: null
                enabled: root.dismissible && !root.popIn
                xAxis.enabled: false
                // The distance is measured in the window's coordinates: the handler's own item moves with the sheet, and a
                // distance in its coordinates would grow with every step.
                onCentroidChanged: {
                    if (active)
                        root.dragOffset = Math.max(0, centroid.scenePosition.y - centroid.scenePressPosition.y);
                }
                onActiveChanged: {
                    if (!active)
                        root.releaseDrag();
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                y: Theme.space.tileGap - grabber.y
                width: Theme.size.badge * 3 / 2
                height: Theme.border.hairline * 5
                radius: height / 2
                color: Theme.colour.border
                Accessible.ignored: true
            }
        }

        // The content area: below the grabber, above the home indicator or the keyboard.
        Item {
            id: body

            objectName: "sheetContent"
            x: root.centred ? root.bodyPadding : 0
            y: root.centred ? Theme.size.badge : root.grabberZone
            width: parent.width - 2 * x
            height: root.panelHeight - y - (root.centred ? Theme.space.screenMargin : root.bottomReserve)
        }
    }

    // ---- motion ----

    ParallelAnimation {
        id: presentAnimation

        YAnimator {
            target: panel
            from: root.restY + (root.popIn ? 0 : Theme.motion.travel(root.height - root.restY))
            to: root.restY
            duration: Theme.motion.sheetPresentMs
            easing.type: Theme.motion.easingSpring
        }
        ScaleAnimator {
            target: panel
            from: root.popIn && !Theme.reduceMotion ? Theme.motion.tilePressScale : 1
            to: 1
            duration: Theme.motion.sheetPresentMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator {
            target: panel
            from: root.popIn || Theme.reduceMotion ? 0 : 1
            to: 1
            duration: Theme.motion.sheetPresentMs
        }
        OpacityAnimator {
            target: scrim
            from: 0
            to: root.scrimLevel
            duration: Theme.motion.sheetPresentMs
        }
        onFinished: {
            root.bindGeometry();
            if (modalState.open && !modalState.closing)
                root.opened();
        }
    }

    ParallelAnimation {
        id: dismissAnimation

        YAnimator {
            target: panel
            from: panel.y
            to: panel.y + (root.popIn ? 0 : Theme.motion.travel(root.height - panel.y))
            duration: Theme.motion.sheetPresentMs
            easing.type: Theme.motion.easingSpring
        }
        ScaleAnimator {
            target: panel
            from: 1
            to: root.popIn && !Theme.reduceMotion ? Theme.motion.tilePressScale : 1
            duration: Theme.motion.sheetPresentMs
            easing.type: Theme.motion.easingSpring
        }
        OpacityAnimator {
            target: panel
            from: 1
            to: root.popIn || Theme.reduceMotion ? 0 : 1
            duration: Theme.motion.sheetPresentMs
        }
        OpacityAnimator {
            target: scrim
            from: scrim.opacity
            to: 0
            duration: Theme.motion.sheetPresentMs
        }
        onFinished: root.finishClose()
    }

    // A drag that did not reach the distance: the sheet goes back to its rest position.
    YAnimator {
        id: snapBack

        target: panel
        from: Theme.reduceMotion ? root.restY : root.snapFrom
        to: root.restY
        duration: Theme.motion.swipeReleaseMs
        easing.type: Theme.motion.easingSpring
        onFinished: root.bindGeometry()
    }
}
