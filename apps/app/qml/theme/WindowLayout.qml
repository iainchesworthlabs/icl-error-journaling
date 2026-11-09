// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// WindowLayout: the layout class of the window and the sizes that follow from it (Stage 8, package 8.1;
// docs/plan/stage-8.md, "Layout approach"). The class is chosen from the window's size in points and nothing else: not
// the platform, not the device and not the orientation. So a narrow Split View window on an iPad gets the phone's
// layout, the desktop window of Stage 12 gets the iPad's when it is wide, and a test picks a class by giving the window
// a size.
//
//     Compact   narrower than Theme.layout.regularMinWidth (600): the phone layout. Every derived value equals the
//               phone's Theme value, so a phone does not change.
//     Regular   wider, and not Wide: the phone layout in centred columns with larger tiles (an iPad in portrait).
//     Wide      at least Theme.layout.wideMinWidth by wideMinHeight (1100 by 640): the board and the log side by
//               side (an iPad in landscape).
//
// One input is set from outside: the shell binds `windowWidth` and `windowHeight` to its own size (as it binds
// Theme.textScale to the bridge), and the test rig sets them for a component it loads alone. They start at the phone's
// design frame, so a component made without a shell is Compact.
//
// The uses of Theme.frame that were in the app when this was added (8.1) are all the phone's design frame, and stay:
// the implicit sizes of the components (a parent sizes them), the window's first size in Main.qml, the safe-area
// offsets of the sheets and the tab bar, and the launch skeleton, which draws the phone's launch image.
QtObject {
    id: root

    enum SizeClass {
        Compact,
        Regular,
        Wide
    }

    // ---- inputs ----

    property real windowWidth: Theme.frame.width
    property real windowHeight: Theme.frame.height

    // ---- the class ----

    readonly property int sizeClass: {
        if (windowWidth >= Theme.layout.wideMinWidth && windowHeight >= Theme.layout.wideMinHeight)
            return WindowLayout.Wide;
        if (windowWidth >= Theme.layout.regularMinWidth)
            return WindowLayout.Regular;
        return WindowLayout.Compact;
    }
    readonly property bool compact: sizeClass === WindowLayout.Compact
    readonly property bool regular: sizeClass === WindowLayout.Regular
    readonly property bool wide: sizeClass === WindowLayout.Wide

    // ---- derived sizes ----

    readonly property real screenMargin: compact ? Theme.space.screenMargin : Theme.layout.screenMargin
    readonly property real tileGap: compact ? Theme.space.tileGap : Theme.layout.tileGap
    // The board's text and count badge: the phone's in Compact, larger on the larger tiles of a larger window.
    readonly property var tileLabelRole: compact ? Theme.font.tileLabel : Theme.font.tileLabelLarge
    readonly property var badgeRole: compact ? Theme.font.badge : Theme.font.badgeLarge
    readonly property real badge: compact ? Theme.size.badge : Theme.layout.badge

    // The width of a centred column no wider than `limit`, inside the screen margins. A Compact window has no
    // limit: its columns are the full width less the margins, as on the phone.
    function columnWidth(limit: real): real {
        const full = Math.max(0, windowWidth - 2 * screenMargin);
        return compact ? full : Math.min(limit, full);
    }

    // The margin on each side of a centred column no wider than `limit`, in a space `available` wide (a screen's own
    // width, which is the window's less any side insets). In a Compact window it is the phone's screen margin, so
    // `x: sideMargin(w, limit)` and `width: w - 2 * sideMargin(w, limit)` are the phone's layout to the point; else it
    // is at least the larger margin and as much more as keeps the column within its limit. Rounded to a whole point.
    function sideMargin(available: real, limit: real): real {
        if (compact)
            return Theme.space.screenMargin;
        return Math.max(screenMargin, Math.round((available - limit) / 2));
    }

    readonly property real logColumnWidth: columnWidth(Theme.layout.logColumn)
    readonly property real formColumnWidth: columnWidth(Theme.layout.formColumn)
    readonly property real readingColumnWidth: columnWidth(Theme.layout.readingColumn)
    readonly property real firstLaunchColumnWidth: columnWidth(Theme.layout.firstLaunchColumn)

    // The two panes of the Wide Log screen: the board's share of the width inside the margins, less the gap between
    // them, rounded to a whole point; the log has the rest. Zero in the other classes.
    readonly property real panesWidth: Math.max(0, windowWidth - 2 * screenMargin - Theme.layout.paneGap)
    readonly property real boardPaneWidth: wide ? Math.round(panesWidth * Theme.layout.boardShare) : 0
    readonly property real logPaneWidth: wide ? panesWidth - boardPaneWidth : 0

    // The height of a board tile `tileWidth` wide in a column: the phone's in Compact, else a fixed share of the width
    // between the phone's height and Theme.layout.tileHeightMax. (A Wide board fills the height of its pane instead.)
    function tileHeightFor(tileWidth: real): real {
        if (compact)
            return Theme.size.tileHeight;
        const share = tileWidth * Theme.layout.tileHeightRatio;
        return Math.round(Math.max(Theme.size.tileHeight, Math.min(Theme.layout.tileHeightMax, share)));
    }
}
