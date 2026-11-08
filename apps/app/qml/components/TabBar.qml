// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The tab bar (Stage 4, package 4.4): Log, Review, History and Settings along the bottom of the window.
//
// It is 83 pt tall on an iPhone 17, which includes the 34 pt home indicator inset: the four tabs fill the 49 pt above the
// inset and the bar's background runs under it. The shell reads the inset from its SafeFrame (the only reader of the
// insets) and hands it over as `bottomInset`, so a phone with a different inset gets a taller bar and the tabs stay clear
// of the indicator. Place the bar with anchors at the bottom of the window and leave its height alone.
//
// The selected tab is shown by its colour and by a heavier label (600 against 500), as in the mock-ups, so colour is never
// the only cue. The icon sits 8 pt below the 1 pt top border and the label 3 pt under it; at a large text size the padding
// gives way only as far as it must to keep the label above the inset. To a screen reader the bar is a tab list and each
// tab a selectable page tab. `skeleton` draws neutral shapes with no
// text for the launch screen, and takes no input.
Item {
    id: root

    // The bottom safe-area inset, from the shell's SafeFrame.
    required property real bottomInset
    // The index of the selected tab, 0 for Log. A tap on another tab sets it, so a binding to it is replaced; use
    // `tabSelected` to follow the change instead.
    property int currentIndex: 0
    // Shapes only, in neutral colours, with no text and no input.
    property bool skeleton: false

    // The user chose a tab other than the current one; `currentIndex` already has the new value.
    signal tabSelected(int index)
    // The user chose the tab that is already current (the shell may pop to the root of that tab).
    signal tabReselected(int index)

    // The part of the bar above the inset.
    readonly property real contentHeight: Theme.size.tabBarHeight - Theme.frame.homeIndicator
    readonly property var tabs: [
        {
            "key": "Log",
            "label": qsTr("Log"),
            "icon": "board"
        },
        {
            "key": "Review",
            "label": qsTr("Review"),
            "icon": "list"
        },
        {
            "key": "History",
            "label": qsTr("History"),
            "icon": "calendar"
        },
        {
            "key": "Settings",
            "label": qsTr("Settings"),
            "icon": "sun"
        }
    ]

    function select(index: int): void {
        if (root.skeleton)
            return;
        if (index === root.currentIndex) {
            root.tabReselected(index);
            return;
        }
        root.currentIndex = index;
        root.tabSelected(index);
    }

    implicitWidth: Theme.frame.width
    implicitHeight: contentHeight + bottomInset

    Rectangle {
        id: bar

        objectName: "tabBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: root.implicitHeight
        color: Theme.colour.surface
        Accessible.role: Accessible.PageTabList
        Accessible.name: qsTr("Tabs")
        Accessible.ignored: root.skeleton

        Rectangle {
            objectName: "tabBarBorder"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.border.hairline
            color: Theme.colour.tabBarBorder
            Accessible.ignored: true
        }

        Repeater {
            model: root.tabs

            delegate: Item {
                id: tab

                required property int index
                required property var modelData
                readonly property bool current: tab.index === root.currentIndex
                readonly property color tint: tab.current ? Theme.colour.primaryText : Theme.colour.textSecondary
                // The icon is 8 pt below the 1 pt border. Only when a large label would otherwise reach the inset does the
                // padding give way, as far as it must.
                readonly property real topPadding: Math.max(Theme.border.hairline, Math.min(Theme.border.hairline + Theme.space.tileGap, root.contentHeight - Theme.size.badge - labelGap - label.height))
                // The gap between the icon and the label.
                readonly property real labelGap: Theme.border.otherWidth * 2

                objectName: "tab" + tab.modelData.key
                x: tab.index * bar.width / root.tabs.length
                y: 0
                width: bar.width / root.tabs.length
                height: root.contentHeight
                Accessible.ignored: root.skeleton
                Accessible.role: Accessible.PageTab
                Accessible.name: tab.modelData.label
                Accessible.description: qsTr("%1 of %2").arg(tab.index + 1).arg(root.tabs.length)
                Accessible.selectable: true
                Accessible.selected: tab.current
                Accessible.focusable: true
                Accessible.onPressAction: root.select(tab.index)

                TapHandler {
                    enabled: !root.skeleton
                    onTapped: root.select(tab.index)
                }

                Icon {
                    id: icon

                    objectName: "tabIcon"
                    visible: !root.skeleton
                    x: (tab.width - width) / 2
                    y: tab.topPadding
                    kind: tab.modelData.icon
                    colour: tab.tint
                }

                Text {
                    id: label

                    visible: !root.skeleton
                    x: 0
                    y: tab.topPadding + Theme.size.badge + tab.labelGap
                    width: tab.width
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    color: tab.tint
                    font: tab.current ? Theme.font.tabLabel.selectedFont : Theme.font.tabLabel.font
                    text: tab.modelData.label
                    Accessible.ignored: true
                }

                // The launch screen's stand-ins: a rounded square for the icon and a short bar for the label.
                Rectangle {
                    objectName: "tabSkeletonIcon"
                    visible: root.skeleton
                    x: (tab.width - width) / 2
                    y: tab.topPadding
                    width: Theme.size.badge
                    height: Theme.size.badge
                    radius: Theme.radius.field / 2
                    color: Theme.colour.swatch
                    Accessible.ignored: true
                }

                Rectangle {
                    objectName: "tabSkeletonLabel"
                    visible: root.skeleton
                    x: (tab.width - width) / 2
                    y: tab.topPadding + Theme.size.badge + tab.labelGap
                    width: Theme.size.badge * 2
                    height: Theme.font.tabLabel.size
                    radius: height / 2
                    color: Theme.colour.swatch
                    Accessible.ignored: true
                }
            }
        }
    }
}
