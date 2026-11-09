// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The on-screen keyboard, as the one place that reads it (assumption U5) and the one place that puts it away. Nothing else
// in the app reads `Qt.inputMethod`: SafeFrame and Sheet ask `coverage()` how far the keyboard reaches into them, and the
// shell and the sheets call `dismiss()` and `hide()` when the person is done typing.
//
// What Qt reports. `keyboardRectangle` is in window coordinates, and since the window has `Qt.ExpandedClientAreaHint` the
// window is the whole screen, so the rectangle's bottom edge is the window's. Qt's iOS plugin (qiosinputcontext.mm,
// `updateKeyboardState`) gives the rectangle of the start of the transition on a "will show" notification, which is below
// the window, and the final one on "did change frame". The size is right throughout and the top is final only at the end,
// so `coverage()` takes the rectangle's top once the keyboard is on screen and the window's height less the rectangle's
// height until then. A keyboard that is not docked to the bottom of the window (floating on an iPad) covers nothing.
// Qt also scrolls its own root view when the cursor would be under the keyboard (`scrollToCursor`), which draws the whole
// app, bars and status bar included, shifted up: it does nothing when the focused field is above the keyboard, so every
// screen has to keep its field there, which is what SafeFrame's `avoidKeyboard` does.
//
// A test has no keyboard offscreen. It sets `overrideKeyboard`, `overrideVisible` and `overrideRectangle` (the rectangle
// that iOS reports, in the window), and reads `hideCount` for the times the app put the keyboard away; `hide()` then takes
// the override keyboard down, so a test sees whether the app did it.
QtObject {
    id: root

    // Use the override* values and ignore Qt's input method. Tests set this; the app never does.
    property bool overrideKeyboard: false
    property bool overrideVisible: false
    property rect overrideRectangle: Qt.rect(0, 0, 0, 0)
    // Print every change of the rectangle, for the device build (checklist V1).
    property bool logChanges: false

    // Qt's input method, untyped because the QML type system sees it only as a QObject.
    readonly property var inputMethod: Qt.inputMethod
    readonly property bool visible: overrideKeyboard ? overrideVisible : inputMethod.visible
    readonly property rect rectangle: overrideKeyboard ? overrideRectangle : inputMethod.keyboardRectangle
    // How many times `hide()` ran: what a test can see of the keyboard going away.
    property int hideCount: 0

    // How far the keyboard reaches up into `item` from the bottom edge of the item, in points of the item; 0 when there is
    // none. It is read in a binding, so it follows the keyboard and the size of the item.
    function coverage(item: Item): real {
        if (!visible || item === null)
            return 0;
        const window = item.Window.window;
        const rect = rectangle;
        if (window === null || rect.height <= 0)
            return 0;
        const windowBottom = window.height;
        // Not docked to the bottom of the window: it floats over the page and nothing is moved for it.
        if (rect.y + rect.height < windowBottom - 1)
            return 0;
        const top = rect.y < windowBottom ? rect.y : windowBottom - rect.height;
        const bottom = item.mapToItem(null, 0, item.height).y;
        return Math.max(0, bottom - top);
    }

    // Whether `item` is something that the keyboard types into (a TextInput, a TextEdit or a control made of one).
    function isTextInput(item: var): bool {
        return item !== null && item !== undefined && item.inputMethodHints !== undefined;
    }

    // The text field in `item`'s window that has the focus, or null.
    function focusedInput(item: Item): var {
        const window = item !== null ? item.Window.window : null;
        const focused = window !== null ? window.activeFocusItem : null;
        return isTextInput(focused) ? focused : null;
    }

    // Puts the keyboard away. The override keyboard obeys, as the phone's does.
    function hide(): void {
        hideCount += 1;
        if (overrideKeyboard) {
            overrideVisible = false;
            overrideRectangle = Qt.rect(0, 0, 0, 0);
        } else {
            inputMethod.hide();
        }
    }

    // Ends the typing in `item`'s window: the text field that has the focus loses it, which is the field's own commit, and the
    // keyboard goes. A field inside `keep` (the page that has just come in) is left alone.
    function dismiss(item: Item, keep: Item): void {
        const input = focusedInput(item);
        if (input === null) {
            if (visible)
                hide();
            return;
        }
        for (let owner = input; keep !== null && owner !== null; owner = owner.parent) {
            if (owner === keep)
                return;
        }
        input.focus = false;
        hide();
    }

    // Scrolls `flick` so that `field`, an item in its content, is whole in view with room to spare at the bottom, which Qt's own
    // check for a cursor under the keyboard needs (it pads the cursor by 20 pt), and at the top. `covered` is how much of the
    // top of the Flickable a bar over it hides.
    function reveal(flick: Flickable, field: Item, covered: real): void {
        const margin = Theme.space.screenMargin + Theme.space.sectionGap;
        const top = field.mapToItem(flick.contentItem, 0, 0).y;
        const bottom = top + field.height;
        const lowest = -flick.topMargin;
        const highest = Math.max(lowest, flick.contentHeight + flick.bottomMargin - flick.height);
        if (bottom + margin > flick.contentY + flick.height)
            flick.contentY = Math.min(highest, bottom + margin - flick.height);
        if (top - margin < flick.contentY + covered)
            flick.contentY = Math.max(lowest, top - margin - covered);
    }

    // The same for the text field that has the focus, when it is in the Flickable: for a Flickable that gets shorter when the
    // keyboard comes up, which calls it from `onHeightChanged`, and for the field being given the focus.
    function revealFocused(flick: Flickable, covered: real): void {
        const field = focusedInput(flick);
        if (field === null)
            return;
        for (let owner = field.parent; owner !== null; owner = owner.parent) {
            if (owner === flick.contentItem) {
                reveal(flick, field, covered);
                return;
            }
        }
    }

    onRectangleChanged: {
        if (logChanges)
            console.info("keyboard: x %1, y %2, width %3, height %4".arg(rectangle.x).arg(rectangle.y).arg(rectangle.width).arg(rectangle.height));
    }
}
