// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The licence of the app or of one component (PRF-12): who it is, what it is licensed under, how it is linked, its copyright and
// source, and then the full text of each licence it is offered under. `licenceId` is "app" or the id of a component of the About
// list.
//
// The text is read as paragraphs: each is its own piece of text, so a screen reader moves through the licence a paragraph at a
// time and a person at the largest text size reads lines the width of the screen, not the width of the file. The text of the
// licence is the licence's own; only its line breaks are the model's (AboutModel::paragraphs).
//
// This file is a part of SettingsScreen and is tested through it.
SettingsPage {
    id: root

    required property var about
    required property string licenceId

    readonly property var page: about.licence(licenceId)
    readonly property bool known: page.name !== undefined
    // The licence of the app itself, which has its own title and the app row.
    readonly property bool isApp: licenceId === "app"
    // What the page prints under the facts, in order: a heading or a paragraph each. `kind` is "heading" or "paragraph".
    readonly property var blocks: {
        const result = [];
        if (!known)
            return result;
        if (page.note.length > 0)
            result.push({
                "kind": "paragraph",
                "text": page.note
            });
        if (page.copyright.length > 0) {
            result.push({
                "kind": "heading",
                "text": qsTr("Copyright")
            });
            for (const line of page.copyright.split("\n"))
                result.push({
                    "kind": "paragraph",
                    "text": line
                });
        }
        if (page.source.length > 0) {
            result.push({
                "kind": "heading",
                "text": qsTr("Source")
            });
            result.push({
                "kind": "paragraph",
                "text": page.source
            });
        }
        for (const entry of page.texts) {
            result.push({
                "kind": "heading",
                "text": entry.title
            });
            for (const paragraph of entry.paragraphs)
                result.push({
                    "kind": "paragraph",
                    "text": paragraph
                });
        }
        return result;
    }

    // The page was left.
    signal closeRequested

    // How the library files are linked, in words. Empty when the build could not tell.
    function linkingText(linking: string): string {
        if (linking === "static")
            return qsTr("Static");
        if (linking === "dynamic")
            return qsTr("Dynamic");
        if (linking === "dynamic/static")
            return qsTr("Static and dynamic");
        return "";
    }

    title: known && !isApp ? page.name : qsTr("Licence")
    leadingText: qsTr("About")
    bodySpacing: Theme.space.sectionGap
    onLeadingTriggered: closeRequested()

    GroupedList {
        objectName: "licenceFacts"
        width: parent.width
        visible: root.known

        ListRow {
            objectName: "licenceName"
            visible: root.isApp
            text: qsTr("App")
            value: root.known ? root.page.name : ""
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "licenceVersion"
            visible: root.known && root.page.version.length > 0
            text: qsTr("Version")
            value: root.known ? root.page.version : ""
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "licenceExpression"
            text: qsTr("Licence")
            value: root.known ? root.page.licence : ""
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }

        ListRow {
            objectName: "licenceLinking"
            visible: root.known && root.linkingText(root.page.linking).length > 0
            text: qsTr("Linked")
            value: root.known ? root.linkingText(root.page.linking) : ""
            tappable: false
            minimumHeight: Theme.size.rowHeightCompact
        }
    }

    Text {
        objectName: "licenceMissing"
        width: parent.width
        visible: !root.known
        color: Theme.colour.textPrimary
        font: Theme.font.body.font
        text: qsTr("This licence is not in the list.")
        wrapMode: Text.WordWrap
    }

    Column {
        width: parent.width
        spacing: Theme.space.tileGap

        Repeater {
            model: root.blocks

            Text {
                id: block

                required property var modelData
                readonly property bool isHeading: modelData.kind === "heading"

                objectName: isHeading ? "licenceHeading" : "licenceParagraph"
                x: isHeading ? Theme.space.tileGap / 2 : 0
                width: parent.width - 2 * x
                topPadding: isHeading ? Theme.space.sectionGap : 0
                color: isHeading ? Theme.colour.textSecondary : Theme.colour.textPrimary
                font: isHeading ? Theme.font.sectionLabel.font : Theme.font.rowText.font
                text: isHeading ? modelData.text.toLocaleUpperCase() : modelData.text
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                Accessible.role: Accessible.StaticText
                Accessible.name: modelData.text
            }
        }
    }
}
