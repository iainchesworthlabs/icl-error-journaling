// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The foot of the Settings list (Stage 4, 4.15): the version of the app and which bridge and storage it was built with, in
// small type, as a plain caption. It used to hide the timing readout behind five taps; that moved to the Version row of the
// About page, which also has the commit, the build number, the licence and the components (PRF-12).
//
// This file is a part of SettingsScreen and is tested through it (and through the shell, in app_flow_test.cpp).
Item {
    id: root

    // The version and platform text, such as "0.1.0 · ios, storage: sqlite". Empty hides the whole footer.
    property string aboutText

    visible: aboutText.length > 0
    width: parent ? parent.width : 0
    height: visible ? Math.max(Theme.size.minTouchTarget, caption.implicitHeight) : 0
    Accessible.role: Accessible.StaticText
    Accessible.name: root.aboutText

    Text {
        id: caption

        objectName: "settingsVersionCaption"
        x: Theme.space.tileGap / 2
        width: parent.width - 2 * x
        height: parent.height
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.WordWrap
        color: Theme.colour.textSecondary
        font: Theme.font.caption.font
        text: root.aboutText
        Accessible.ignored: true
    }
}
