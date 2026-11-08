// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The layer that sheets and alerts are presented in (Stage 4, package 4.4). It fills the window, holds the screen as its
// content and the sheets over it, so a sheet covers the tab bar as well as the screen:
//
//     SheetHost {
//         anchors.fill: parent
//         TabBarAndScreens { anchors.fill: parent }
//         sheets: [
//             Sheet { accessibleName: qsTr("Log another error"); ... },
//             Alert { title: qsTr("Delete this log?"); ... }
//         ]
//     }
//
// While any sheet in the layer is open the screen below is disabled, which stops touches, takes keyboard focus away
// from it and marks it unavailable to a screen reader, on top of the scrim that dims it and the focus trap of the sheet.
// Disabling does not change the look: the components dim only when they are disabled themselves (PrimaryButton, the
// trailing action of NavBar, the buttons of MiniBoard), so the screen under the scrim looks as the mock-ups draw it.
// The sheets stack in the order they are declared: a later one is over an earlier one.
Item {
    id: root

    // The screen below the sheets.
    default property alias content: base.data
    // The sheets and alerts.
    property alias sheets: layer.data

    // Whether any sheet or alert in the layer is open.
    readonly property bool modalOpen: {
        for (let i = 0; i < layer.children.length; ++i) {
            if (layer.children[i].isOpen === true)
                return true;
        }
        return false;
    }

    Item {
        id: base

        objectName: "sheetHostContent"
        anchors.fill: parent
        enabled: !root.modalOpen
    }

    Item {
        id: layer

        objectName: "sheetHostLayer"
        anchors.fill: parent
    }
}
