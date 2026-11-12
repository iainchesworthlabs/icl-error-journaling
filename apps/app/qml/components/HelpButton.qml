// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The help button of a page (Stage 10, package 10.6; docs/plan/stage-10.md, "In-app help"): a question mark in a circle,
// drawn at 24 pt in a 44 pt target, as the mock-ups ErrorTypesHelp and ImportErrorsHelp have it. A page puts it in the
// trailing slot of its NavBar by giving the bar a `helpName`, and opens a HelpSheet when it is triggered.
//
// To a screen reader it is a button named by `accessibleName`, such as "Help with error types".
Item {
    id: root

    // What a screen reader says: the topic the button opens, such as "Help with error types". Required, because a button
    // with no name cannot be found.
    required property string accessibleName

    signal triggered

    implicitWidth: Theme.size.minTouchTarget
    implicitHeight: Theme.size.minTouchTarget
    width: implicitWidth
    height: implicitHeight
    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.onPressAction: root.triggered()

    TapHandler {
        onTapped: root.triggered()
    }

    Icon {
        objectName: "helpIcon"
        anchors.centerIn: parent
        kind: "help"
        size: Theme.size.badge
        colour: Theme.colour.primaryText
    }
}
