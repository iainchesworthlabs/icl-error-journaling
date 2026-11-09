// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The window minus the safe-area insets (the Dynamic Island and the home indicator on an iPhone 17). SafeFrame is the
// only reader of the insets (Stage 4, assumption U1): a shell or a screen puts what must stay clear of them inside it,
// and reads `topInset` and `bottomInset` when something has to reach into them (the tab bar's 83 pt includes the home
// indicator inset). Give it the size of the window: `anchors.fill: parent`.
//
// A test, or a phone whose insets differ, sets `overrideInsets` and the four `override*` values; nothing else changes.
//
// With `avoidKeyboard` the frame also ends at the top of the keyboard when one is up (the keyboard is higher than the home
// indicator inset, so it replaces that inset), and the field being typed in is scrolled into view in the Flickable that
// holds it. The top is never touched, so the bar and what scrolls under it stay where the design has them. A screen with a
// text field puts it inside a SafeFrame with `avoidKeyboard: true` and does nothing else about the keyboard. The keyboard is
// read from Keyboard, in the coordinates of the window, whatever the size and position of this item.
Item {
    id: root

    // Use the override* values and ignore the platform's insets. Tests set this; the app never does.
    property bool overrideInsets: false
    property real overrideTop: 0
    property real overrideBottom: 0
    property real overrideLeft: 0
    property real overrideRight: 0
    // Print the insets once the item is complete, for the device build of assumption U1 (checklist V1).
    property bool logInsets: false
    // The frame ends at the keyboard while it is up, and the field being typed in is kept in view.
    property bool avoidKeyboard: false
    // With `avoidKeyboard`: scroll the Flickable that holds the focused field. A screen that does it with its own bar over
    // the Flickable turns this off and scrolls itself.
    property bool revealFocus: true

    readonly property real topInset: overrideInsets ? overrideTop : Theme.overrideSafeArea ? simulatedInset(true) : SafeArea.margins.top
    readonly property real bottomInset: overrideInsets ? overrideBottom : Theme.overrideSafeArea ? simulatedInset(false) : SafeArea.margins.bottom
    readonly property real leftInset: overrideInsets ? overrideLeft : Theme.overrideSafeArea ? simulatedSide(true) : SafeArea.margins.left
    readonly property real rightInset: overrideInsets ? overrideRight : Theme.overrideSafeArea ? simulatedSide(false) : SafeArea.margins.right

    // How far the keyboard reaches up into this item, in its points; 0 without `avoidKeyboard` or a keyboard.
    readonly property real keyboardInset: avoidKeyboard ? Keyboard.coverage(root) : 0

    // The area inside the insets: the item that the children are placed in. Its position and size are in this item's
    // coordinates.
    readonly property Item frame: inner

    default property alias content: inner.data

    Item {
        id: inner

        objectName: "safeFrameInner"
        anchors.fill: parent
        anchors.topMargin: root.topInset
        anchors.bottomMargin: Math.max(root.bottomInset, root.keyboardInset)
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset
    }

    // The part of the safe area that `Theme.overrideSafeArea` simulates that this item touches, at its top or at its bottom.
    function simulatedInset(atTop: bool): real {
        const window = root.Window.window;
        if (window === null)
            return 0;
        if (atTop)
            return Math.max(0, Theme.overrideSafeTop - root.mapToItem(null, 0, 0).y);
        return Math.max(0, Theme.overrideSafeBottom - (window.height - root.mapToItem(null, 0, root.height).y));
    }

    // The same at its left or its right side.
    function simulatedSide(atLeft: bool): real {
        const window = root.Window.window;
        if (window === null)
            return 0;
        if (atLeft)
            return Math.max(0, Theme.overrideSafeLeft - root.mapToItem(null, 0, 0).x);
        return Math.max(0, Theme.overrideSafeRight - (window.width - root.mapToItem(null, root.width, 0).x));
    }

    // Scrolls the Flickable that holds the focused text field, if the frame has one, so that the field is clear of the top and
    // the bottom of it by a margin, which is more than Qt's own check for a cursor under the keyboard asks for.
    function reveal(): void {
        const field = Keyboard.focusedInput(root);
        if (!avoidKeyboard || !revealFocus || field === null)
            return;
        let inside = false;
        let flick = null;
        for (let owner = field.parent; owner !== null; owner = owner.parent) {
            if (flick === null && owner.contentItem !== undefined && owner.contentY !== undefined)
                flick = owner;
            if (owner === inner) {
                inside = true;
                break;
            }
        }
        if (!inside || flick === null)
            return;
        Keyboard.reveal(flick, field, 0);
    }

    onKeyboardInsetChanged: {
        // Now, before Qt looks for the cursor, and once the layout has settled.
        reveal();
        Qt.callLater(root.reveal);
    }

    Connections {
        function onActiveFocusItemChanged(): void {
            Qt.callLater(root.reveal);
        }

        target: root.Window.window
        enabled: root.avoidKeyboard
    }

    Component.onCompleted: {
        if (logInsets)
            console.info("safe area: top %1, bottom %2, left %3, right %4".arg(topInset).arg(bottomInset).arg(leftInset).arg(rightInset));
    }
}
