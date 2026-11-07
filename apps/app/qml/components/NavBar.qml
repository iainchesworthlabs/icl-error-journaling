// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import QtQuick.Shapes
import ErrorJournalingApp

// The title bar of a page or a sheet (Stage 4, package 4.4): a title with an optional action at each end.
//
// Two shapes, one component:
//   compact  a row of at least 44 pt with "Cancel", the title centred and "Save", as the sheets of the design have it
//   large    the actions in a row and the 30 pt title under them, left aligned, as History, Review and Settings have it
// When a compact title would not fit between its actions, which happens at the larger text sizes, it moves to a line of
// its own under them, so no text is cut or wrapped to a sliver.
//
// The bar does not know the safe area: a screen puts it inside its SafeFrame, and a sheet puts it below its grabber.
// Text scales with Dynamic Type without a cap and the bar grows with it; a 44 pt row is the minimum.
Item {
    id: root

    required property string title
    // The leading action, such as "Cancel" or "History". Empty for none.
    property string leadingText
    // What a screen reader says for the leading action when the visible text is not enough, such as "Back to History".
    property string leadingAccessibleName: root.leadingText
    // A chevron before the leading text, for going back.
    property bool leadingBack: false
    property string trailingText
    property string trailingAccessibleName: root.trailingText
    property bool trailingEnabled: true
    // The trailing action is the one that finishes the page, and is set in the heavier weight.
    property bool trailingEmphasised: true
    // The 30 pt title under the actions, instead of the centred one.
    property bool large: false

    signal leadingTriggered
    signal trailingTriggered

    readonly property bool hasActions: leadingText.length > 0 || trailingText.length > 0
    // The widest action, which the centred title keeps clear of on both sides so that it stays centred.
    readonly property real actionWidth: Math.max(leadingItem.visible ? leadingItem.width : 0, trailingItem.visible ? trailingItem.width : 0)
    // A compact title that does not fit between the actions goes on its own line.
    readonly property bool stacked: !large && hasActions && (titleMetrics.width + 2 * (Theme.space.screenMargin + actionWidth + Theme.space.sectionGap) > root.width)
    readonly property bool titleBelow: large || stacked
    readonly property real rowHeight: hasActions || !large ? Math.max(Theme.size.minTouchTarget, leadingItem.height, trailingItem.height, large ? 0 : inlineTitle.implicitHeight) : 0

    implicitWidth: Theme.frame.width
    implicitHeight: rowHeight + (titleBelow ? belowTitle.height : 0)

    TextMetrics {
        id: titleMetrics

        font: Theme.font.sheetTitle.font
        text: root.title
    }

    // ---- the leading action ----

    Item {
        id: leadingItem

        objectName: "navLeading"
        visible: root.leadingText.length > 0
        x: Theme.space.screenMargin
        y: (root.rowHeight - height) / 2
        width: Math.max(Theme.size.minTouchTarget, leadingRow.width)
        height: Math.max(Theme.size.minTouchTarget, leadingRow.height)
        Accessible.role: Accessible.Button
        Accessible.name: root.leadingAccessibleName
        Accessible.onPressAction: root.leadingTriggered()

        TapHandler {
            enabled: leadingItem.visible
            onTapped: root.leadingTriggered()
        }

        Row {
            id: leadingRow

            anchors.verticalCenter: parent.verticalCenter

            Shape {
                visible: root.leadingBack
                width: visible ? Theme.size.badge : 0
                height: Theme.size.badge
                anchors.verticalCenter: parent.verticalCenter
                Accessible.ignored: true

                ShapePath {
                    strokeColor: Theme.colour.primaryText
                    strokeWidth: Theme.border.hairline * 2
                    fillColor: "transparent"
                    capStyle: ShapePath.RoundCap
                    joinStyle: ShapePath.RoundJoin

                    PathSvg {
                        path: "M15 18L9 12L15 6"
                    }
                }
            }

            Text {
                color: Theme.colour.primaryText
                font: Qt.font({
                    "pixelSize": Theme.font.sheetTitle.font.pixelSize,
                    "weight": Font.Normal
                })
                text: root.leadingText
                Accessible.ignored: true
            }
        }
    }

    // ---- the title, on the row between the actions ----

    Text {
        id: inlineTitle

        objectName: "navTitle"
        visible: !root.titleBelow
        x: (root.width - width) / 2
        y: (root.rowHeight - height) / 2
        width: Math.min(titleMetrics.width, root.width - 2 * (Theme.space.screenMargin + root.actionWidth + Theme.space.sectionGap))
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.colour.textPrimary
        font: Theme.font.sheetTitle.font
        text: root.title
        Accessible.role: Accessible.Heading
        Accessible.name: root.title
    }

    // ---- the trailing action ----

    Item {
        id: trailingItem

        objectName: "navTrailing"
        visible: root.trailingText.length > 0
        enabled: root.trailingEnabled
        x: root.width - width - Theme.space.screenMargin
        y: (root.rowHeight - height) / 2
        width: Math.max(Theme.size.minTouchTarget, trailingLabel.implicitWidth)
        height: Math.max(Theme.size.minTouchTarget, trailingLabel.implicitHeight)
        Accessible.role: Accessible.Button
        Accessible.name: root.trailingAccessibleName
        Accessible.onPressAction: root.trailingTriggered()

        TapHandler {
            enabled: trailingItem.visible && trailingItem.enabled
            onTapped: root.trailingTriggered()
        }

        Text {
            id: trailingLabel

            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            color: trailingItem.enabled ? Theme.colour.primaryText : Theme.colour.textSecondary
            font: Qt.font({
                "pixelSize": Theme.font.sheetTitle.font.pixelSize,
                "weight": root.trailingEmphasised ? Font.DemiBold : Font.Normal
            })
            text: root.trailingText
            Accessible.ignored: true
        }
    }

    // ---- the title on its own line: the large title, or a compact one that did not fit ----

    Text {
        id: belowTitle

        objectName: "navTitleBelow"
        visible: root.titleBelow
        x: Theme.space.screenMargin
        y: root.rowHeight
        width: root.width - 2 * Theme.space.screenMargin
        horizontalAlignment: root.large ? Text.AlignLeft : Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.colour.textPrimary
        font: root.large ? Theme.font.largeTitle.font : Theme.font.sheetTitle.font
        text: root.title
        Accessible.role: Accessible.Heading
        Accessible.name: root.title
    }
}
