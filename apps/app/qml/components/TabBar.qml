// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// The tab bar (Stage 4, package 4.4): Log, Review, History and Settings along the bottom of the window.
//
// It is 83 pt tall on an iPhone 17, which includes the 34 pt home indicator inset: the four tabs fill the 49 pt above the
// inset and the bar's background runs under it. The shell reads the inset from its SafeFrame (the only reader of the
// insets) and hands it over as `bottomInset`, so a phone with a different inset gets a taller bar and the tabs stay clear
// of the indicator. Place the bar with anchors at the bottom of the window and leave its height alone.
//
// The selected tab is shown by its colour, by a heavier label and by a short bar over the icon, so colour is never the only
// cue. To a screen reader the bar is a tab list and each tab a selectable page tab. `skeleton` draws neutral shapes with no
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
            "icon": "M4.5 3H8.5A1.5 1.5 0 0 1 10 4.5V8.5A1.5 1.5 0 0 1 8.5 10H4.5A1.5 1.5 0 0 1 3 8.5V4.5A1.5 1.5 0 0 1 4.5 3Z " + "M15.5 3H19.5A1.5 1.5 0 0 1 21 4.5V8.5A1.5 1.5 0 0 1 19.5 10H15.5A1.5 1.5 0 0 1 14 8.5V4.5A1.5 1.5 0 0 1 15.5 3Z " + "M4.5 14H8.5A1.5 1.5 0 0 1 10 15.5V19.5A1.5 1.5 0 0 1 8.5 21H4.5A1.5 1.5 0 0 1 3 19.5V15.5A1.5 1.5 0 0 1 4.5 14Z " + "M15.5 14H19.5A1.5 1.5 0 0 1 21 15.5V19.5A1.5 1.5 0 0 1 19.5 21H15.5A1.5 1.5 0 0 1 14 19.5V15.5A1.5 1.5 0 0 1 15.5 14Z"
        },
        {
            "key": "Review",
            "label": qsTr("Review"),
            "icon": "M8 6H21M8 12H21M8 18H21M3 6H3.01M3 12H3.01M3 18H3.01"
        },
        {
            "key": "History",
            "label": qsTr("History"),
            "icon": "M5 4H19A2 2 0 0 1 21 6V19A2 2 0 0 1 19 21H5A2 2 0 0 1 3 19V6A2 2 0 0 1 5 4Z M16 2V6M8 2V6M3 10H21"
        },
        {
            "key": "Settings",
            "label": qsTr("Settings"),
            "icon": "M9 12A3 3 0 1 1 15 12A3 3 0 1 1 9 12Z M12 2V5M12 19V22M4.2 4.2L6.3 6.3M17.7 17.7L19.8 19.8M2 12H5M19 12H22M4.2 19.8L6.3 17.7M17.7 6.3L19.8 4.2"
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
                // The icon and the label are centred in the part above the inset; when a large label leaves less room than
                // the top padding, the padding gives way.
                readonly property real topPadding: Math.max(0, Math.min(Theme.space.tileGap, root.contentHeight - Theme.size.badge - Theme.space.tileGap / 2 - label.height))

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

                // The second cue of the selected tab: a short bar at the top edge.
                Rectangle {
                    objectName: "tabSelectedBar"
                    visible: tab.current && !root.skeleton
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: Theme.border.hairline
                    width: Theme.size.badge
                    height: Theme.border.hairline * 2
                    radius: height / 2
                    color: Theme.colour.primaryText
                    Accessible.ignored: true
                }

                Shape {
                    id: icon

                    visible: !root.skeleton
                    x: (tab.width - width) / 2
                    y: tab.topPadding
                    width: Theme.size.badge
                    height: Theme.size.badge
                    Accessible.ignored: true

                    ShapePath {
                        strokeColor: tab.tint
                        strokeWidth: Theme.border.hairline * 2
                        fillColor: "transparent"
                        capStyle: ShapePath.RoundCap
                        joinStyle: ShapePath.RoundJoin

                        PathSvg {
                            path: tab.modelData.icon
                        }
                    }
                }

                Text {
                    id: label

                    visible: !root.skeleton
                    x: 0
                    y: tab.topPadding + Theme.size.badge + Theme.space.tileGap / 2
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
                    y: tab.topPadding + Theme.size.badge + Theme.space.tileGap / 2
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
