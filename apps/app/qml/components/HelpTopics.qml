// SPDX-License-Identifier: GPL-3.0-or-later
pragma Singleton
import QtQuick

// The in-app help (Stage 10, package 10.6; docs/plan/stage-10.md, "In-app help"): the topics that the help buttons, the
// Quick help list in Settings and the HelpSheet show, and the addresses of the online guide that go with them.
//
// The help is text in the app and works without a connection. A topic is a short heading and two to five paragraphs, and
// each names the section of the guide that says more (`page` and `anchor`, relative to the site). The text is the same
// wherever it is shown because it is held here and nowhere else. `tools/ci/check_help_links.py` holds every `page` and
// `anchor` to a heading of the guide, and `siteUrl` is held to `site_url` in `mkdocs.yml` by
// `tools/ci/check_site_address.py`.
//
// A paragraph may use <b> for the words of a button, as the guide does. The words are the app's own.
QtObject {
    id: root

    // Where the documentation site is. It is the one address in the app: change `site_url` in `mkdocs.yml` and this line
    // together (docs/ci.md, "The site address"); a check fails until they agree. Nothing else in the app names the host.
    readonly property string siteUrl: "https://iainchesworthlabs.github.io/icl-error-journaling/"

    // The pages the Help group of Settings opens, as paths under the site.
    readonly property string guidePath: ""
    readonly property string supportPath: "support/"
    readonly property string privacyPath: "privacy/"
    // The page that explains every message of a refused setup file, for the row of the import errors screen.
    readonly property string setupFileFixPage: "guide/setup-file/"
    readonly property string setupFileFixAnchor: "when-the-app-refuses-a-file"

    // The topics, in the order of the Quick help list. `id` is what a help button asks for.
    readonly property var topics: [
        {
            "id": "logging",
            "title": qsTr("Logging an error"),
            "paragraphs": [qsTr("<b>Double-tap</b> a tile to log that error. One tap logs nothing: it only shows a hint, so a touch by accident never makes an entry."), qsTr("A message with <b>Undo</b> shows for five seconds. Double-tap the same tile while it shows and the count goes up: ×2, ×3."), qsTr("<b>Hold</b> a tile to log it with a note. If you typed a note first, the entry takes it. If not, the entry opens so you can type one."), qsTr("Tap <b>Other…</b> for every error type, with a search field. To change or delete an entry later, use the Review tab.")],
            "page": "guide/logging/",
            "anchor": "log-an-error-double-tap"
        },
        {
            "id": "board",
            "title": qsTr("Setting up the board"),
            "paragraphs": [qsTr("Your board has 11 tiles and the fixed <b>Other…</b> tile."), qsTr("<b>Hold and drag</b> a tile to move it."), qsTr("<b>Tap</b> a tile to change its error type. A type that is already on the board swaps places."), qsTr("Tap <b>+</b> on an empty place to fill it, or choose <b>New error type…</b> to make one."), qsTr("Types that are not on the board are under <b>Other…</b> on the Log screen.")],
            "page": "guide/setup/",
            "anchor": "your-board"
        },
        {
            "id": "errorTypes",
            "title": qsTr("Error types"),
            "paragraphs": [qsTr("<b>Error types</b> is the whole list of what you can log. Tap <b>Add an error type</b> to make a new one."), qsTr("The first 11 types on the list are the board, in the order shown. The rest are under <b>Other…</b> on the Log screen."), qsTr("Drag the handle, or use the arrows, to change the order. Tap a type to rename or recolour it."), qsTr("A type you retire leaves the board and Other. It stays on the entries already logged.")],
            "page": "guide/setup/",
            "anchor": "error-types"
        },
        {
            "id": "templates",
            "title": qsTr("Note templates"),
            "paragraphs": [qsTr("A template is a note you log often. Its chip is above the note field on the Log screen."), qsTr("<b>Name</b> is what the chip says. <b>Question to ask</b> is what you are asked when you tap it, for example “Which song?”."), qsTr("<b>Line in the log</b> is what gets written. Tap <b>Insert answer</b> to put the answer where you want it, for example “Played [answer]”."), qsTr("The <b>Example</b> at the bottom shows the line as it will appear.")],
            "page": "guide/setup/",
            "anchor": "note-templates"
        },
        {
            "id": "details",
            "title": qsTr("Today’s details"),
            "paragraphs": [qsTr("The card at the top of the Log screen heads the log you share: the unit, the place, the software version, the date and your name. You can log before every field is filled in."), qsTr("<b>Unit number:</b> type the number only. The unit prefix from Settings is added for you."), qsTr("<b>Software version:</b> numbers only, such as 1.4.2. <b>Commit</b> is the short code of the exact build. Leave it empty if you can’t get it."), qsTr("If the version changes during the day, change it here and choose <b>It changed during the day</b>. The entries before the change keep the old version.")],
            "page": "guide/todays-details/",
            "anchor": ""
        },
        {
            "id": "setupFile",
            "title": qsTr("Import or export a setup file"),
            "paragraphs": [qsTr("A setup file holds your board, locations, note templates and preferences. It has no logs in it."), qsTr("<b>Export this setup…</b> makes a file to send to another phone or person. <b>Import a setup file…</b> reads one."), qsTr("Importing <b>replaces</b> your setup and can’t be undone. Export yours first if you want to keep it. Your logs are not touched."), qsTr("If a file has problems, the app lists every one, in the order they appear in the file, and changes nothing.")],
            "page": "guide/setup/",
            "anchor": "importing-and-exporting-a-setup-file"
        },
        {
            "id": "review",
            "title": qsTr("Review and sharing"),
            "paragraphs": [qsTr("<b>Review</b> is today’s log, in the same words that you share."), qsTr("Tap an entry to edit it. Swipe an entry left for <b>Copy</b> and <b>Delete</b>."), qsTr("<b>Share log</b> opens the share sheet with the log as plain text. Only Share log marks the log as shared. <b>Copy log</b> does not."), qsTr("A past log is in History, where you can share it again.")],
            "page": "guide/sharing/",
            "anchor": "review"
        },
        {
            "id": "interactionTypes",
            "title": qsTr("Interaction types"),
            "paragraphs": [qsTr("<b>Interaction types</b> are the tiles of the optional Interactions board: what you did around the unit. Tap <b>Add an interaction type</b> to make one."), qsTr("<b>Line in the log</b> is what gets written. Tap <b>Insert person</b> to put <b>[person]</b> where the people go, for example “Spoke with [person]”. A type with <b>[person]</b> asks who after you log it. A type without it never asks."), qsTr("Tap <b>Insert answer</b> to put <b>[answer]</b> in the line, and you are asked for an answer after you log it. With no line, the log shows the name."), qsTr("The first 11 types fill the board in the order shown. The rest are under <b>Other…</b>. A type you retire stays on the entries already logged.")],
            "page": "guide/setup/",
            "anchor": "the-second-board-interactions-optional"
        },
        {
            "id": "people",
            "title": qsTr("People"),
            "paragraphs": [qsTr("<b>People</b> are the names an interaction such as “Spoke with [person]” can use. They are optional: a tile with no <b>[person]</b> in its line never asks who."), qsTr("Type a name under <b>New person</b> and tap <b>Add person</b>. Choose a location if the person belongs to one, and <b>Everyone</b> in the picker ticks the people of the location in force."), qsTr("Tap a name to rename it (past entries follow), to <b>Retire</b> it (it leaves the picker and stays on past entries), or to <b>Forget</b> it. Forget removes the name from this device for good, and the entries show “someone”."), qsTr("Names stay on this device. A log you share shows them as initials, in full or as “someone”, as you choose under <b>Names in shared logs</b>.")],
            "page": "guide/setup/",
            "anchor": "people"
        }
    ]

    readonly property string guideAddress: siteUrl + guidePath
    readonly property string supportAddress: siteUrl + supportPath
    readonly property string privacyAddress: siteUrl + privacyPath
    readonly property string setupFileFixAddress: siteUrl + setupFileFixPage + "#" + setupFileFixAnchor

    // The topic with this id, or null.
    function topic(id: string): var {
        for (let i = 0; i < topics.length; ++i) {
            if (topics[i].id === id)
                return topics[i];
        }
        return null;
    }

    // The section of the guide that says more about a topic: the site, the page and, when there is one, the heading.
    function topicAddress(id: string): string {
        const found = topic(id);
        if (!found)
            return siteUrl;
        return siteUrl + found.page + (found.anchor.length > 0 ? "#" + found.anchor : "");
    }
}
