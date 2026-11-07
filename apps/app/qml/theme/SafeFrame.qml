// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The window minus the safe-area insets (the Dynamic Island and the home indicator on an iPhone 17). SafeFrame is the
// only reader of the insets (Stage 4, assumption U1): a shell or a screen puts what must stay clear of them inside it,
// and reads `topInset` and `bottomInset` when something has to reach into them (the tab bar's 83 pt includes the home
// indicator inset). Give it the size of the window: `anchors.fill: parent`.
//
// A test, or a phone whose insets differ, sets `overrideInsets` and the four `override*` values; nothing else changes.
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

    readonly property real topInset: overrideInsets ? overrideTop : SafeArea.margins.top
    readonly property real bottomInset: overrideInsets ? overrideBottom : SafeArea.margins.bottom
    readonly property real leftInset: overrideInsets ? overrideLeft : SafeArea.margins.left
    readonly property real rightInset: overrideInsets ? overrideRight : SafeArea.margins.right

    // The area inside the insets: the item that the children are placed in. Its position and size are in this item's
    // coordinates.
    readonly property Item frame: inner

    default property alias content: inner.data

    Item {
        id: inner

        objectName: "safeFrameInner"
        anchors.fill: parent
        anchors.topMargin: root.topInset
        anchors.bottomMargin: root.bottomInset
        anchors.leftMargin: root.leftInset
        anchors.rightMargin: root.rightInset
    }

    Component.onCompleted: {
        if (logInsets)
            console.info("safe area: top %1, bottom %2, left %3, right %4".arg(topInset).arg(bottomInset).arg(leftInset).arg(rightInset));
    }
}
