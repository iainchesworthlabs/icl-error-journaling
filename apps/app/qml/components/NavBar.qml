// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The title bar of a page or a sheet (Stage 4, package 4.4): a title with an optional action at each end.
//
// Two shapes, one component:
//   compact  a row of at least 44 pt with "Cancel", the title centred and "Save", as the sheets of the design have it
//   large    the actions in a row and the 30 pt title under them, left aligned, as History, Review and Settings have it.
//            There are 8 pt between the row of actions and the title (none when there are no actions) and 8 pt under
//            the title, so the first thing under the bar sits where the mock-ups put it.
// When a compact title would not fit between its actions, which happens at the larger text sizes, it moves to a line of
// its own under them, so no text is cut or wrapped to a sliver. A compact title that fits is never wrapped.
//
// Options:
//   inSheet         the compact bar of a sheet: 8 pt above the row and 16 pt under it, as the sheets of the design have it
//   scrollPosition  how far the page under a large bar has scrolled (a screen binds its Flickable's contentY). Once the
//                   page has scrolled, the bar is `collapsed`: the large title fades out and the compact bar fades in,
//                   with the title centred in the row in 17/600 and a 1 pt divider under the row (HeaderNewLogRow).
//
// The back chevron (`leadingBack`) is 20 pt with a 2.2 stroke and 2 pt from its label; the actions are 17 pt, the
// leading one in 400 and the trailing one in 600 when it is emphasised, centred on the row.
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
    // The compact bar of a sheet, with the padding of the design around its row.
    property bool inSheet: false
    // How far the content under a large bar has scrolled; past 0 the bar collapses to its compact form.
    property real scrollPosition: 0

    signal leadingTriggered
    signal trailingTriggered

    readonly property bool hasActions: leadingText.length > 0 || trailingText.length > 0
    // A large bar whose page has scrolled: the compact form with a divider.
    readonly property bool collapsed: large && scrollPosition > 0
    // The widest action, which the centred title keeps clear of on both sides so that it stays centred.
    readonly property real actionWidth: Math.max(leadingItem.visible ? leadingItem.width : 0, trailingItem.visible ? trailingItem.width : 0)
    // The room between the actions for a centred title.
    readonly property real inlineRoom: root.width - 2 * (Theme.space.screenMargin + actionWidth + Theme.space.sectionGap)
    // The width of the title on one line, rounded up, so that a title that fits is never wrapped by a fraction.
    readonly property real titleWidth: Math.ceil(titleMetrics.advanceWidth) + Theme.border.hairline
    // A compact title that does not fit between the actions goes on its own line.
    readonly property bool stacked: !large && hasActions && titleWidth > inlineRoom
    readonly property bool titleBelow: large || stacked
    readonly property real rowTop: inSheet ? Theme.space.tileGap : 0
    readonly property real rowBottom: inSheet ? Theme.space.screenMargin : 0
    readonly property real rowHeight: hasActions || !large || collapsed ? Math.max(Theme.size.minTouchTarget, leadingItem.height, trailingItem.height, large && !collapsed ? 0 : inlineTitle.implicitHeight) : 0
    // The gaps around a large title: 8 pt from the row of actions, when there is one, and 8 pt under the title.
    readonly property real titleGapAbove: large && rowHeight > 0 ? Theme.space.tileGap : 0
    readonly property real titleGapBelow: large ? Theme.space.tileGap : 0
    readonly property real belowHeight: titleBelow && !collapsed ? titleGapAbove + belowTitle.height + titleGapBelow : 0

    implicitWidth: Theme.frame.width
    implicitHeight: rowTop + rowHeight + rowBottom + belowHeight + (collapsed ? Theme.border.hairline : 0)

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
        y: root.rowTop + (root.rowHeight - height) / 2
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
            spacing: root.leadingBack ? Theme.border.hairline * 2 : 0

            Icon {
                objectName: "navBackChevron"
                visible: root.leadingBack
                anchors.verticalCenter: parent.verticalCenter
                kind: "chevronLeft"
                size: Theme.size.badge * 5 / 6
                strokeWidth: Theme.border.hairline * 2.2
                colour: Theme.colour.primaryText
            }

            Text {
                objectName: "navLeadingLabel"
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.colour.primaryText
                font: Theme.font.navAction.font
                text: root.leadingText
                Accessible.ignored: true
            }
        }
    }

    // ---- the title, on the row between the actions ----

    Text {
        id: inlineTitle

        objectName: "navTitle"
        visible: !root.titleBelow || root.collapsed
        x: (root.width - width) / 2
        y: root.rowTop + (root.rowHeight - height) / 2
        width: Math.max(0, Math.min(root.titleWidth, root.inlineRoom))
        horizontalAlignment: Text.AlignHCenter
        wrapMode: root.titleWidth <= root.inlineRoom ? Text.NoWrap : Text.WordWrap
        color: Theme.colour.textPrimary
        font: Theme.font.sheetTitle.font
        text: root.title
        Accessible.role: Accessible.Heading
        Accessible.name: root.title

        // The compact title fades in as the large one goes (with Reduce Motion, in 100 ms).
        OpacityAnimator on opacity {
            running: root.collapsed
            from: 0
            to: 1
            duration: Theme.motion.toastMs
        }
    }

    // ---- the trailing action ----

    Item {
        id: trailingItem

        objectName: "navTrailing"
        visible: root.trailingText.length > 0
        enabled: root.trailingEnabled
        x: root.width - width - Theme.space.screenMargin
        y: root.rowTop + (root.rowHeight - height) / 2
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
            // The bar's own state, not the inherited one: a sheet over the page disables it without greying it.
            color: root.trailingEnabled ? Theme.colour.primaryText : Theme.colour.textSecondary
            font: root.trailingEmphasised ? Theme.font.navAction.selectedFont : Theme.font.navAction.font
            text: root.trailingText
            Accessible.ignored: true
        }
    }

    // ---- the title on its own line: the large title, or a compact one that did not fit ----

    Text {
        id: belowTitle

        objectName: "navTitleBelow"
        visible: root.titleBelow && !root.collapsed
        x: Theme.space.screenMargin
        y: root.rowTop + root.rowHeight + root.titleGapAbove
        width: root.width - 2 * Theme.space.screenMargin
        horizontalAlignment: root.large ? Text.AlignLeft : Text.AlignHCenter
        wrapMode: Text.WordWrap
        color: Theme.colour.textPrimary
        font: root.large ? Theme.font.largeTitle.font : Theme.font.sheetTitle.font
        text: root.title
        Accessible.role: Accessible.Heading
        Accessible.name: root.title
    }

    // ---- the divider of the collapsed bar ----

    Rectangle {
        objectName: "navDivider"
        visible: root.collapsed
        x: Theme.space.screenMargin
        y: root.rowTop + root.rowHeight + root.rowBottom
        width: root.width - 2 * Theme.space.screenMargin
        height: Theme.border.hairline
        color: Theme.colour.divider
        Accessible.ignored: true
    }
}
