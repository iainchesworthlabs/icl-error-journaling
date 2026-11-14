// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The plum card at the top of the Log screen: the unit and the site and version on the left, the date and the
// tester on the right. A tap opens Today's details. It shows what it is given; a field that is empty reads as a
// prompt ("Location not set") and not as a gap, and the fields that are missing are listed in a strip inside the card
// (HDR-06), which carries a mark and a count so that it is not colour alone. Nothing here blocks logging.
//
// `missingFields` is `session.missingFields`, the keys of the empty fields. With `skeleton` the card draws its shapes
// only, in the neutral swatch colour and with no text, for the launch screen (Stage 5); the geometry is the same. The
// shapes keep the room of the lines that the font gives them, so that the card has the height it will have once the values
// are in. `nominalMetrics` takes the lines at the pitch of the phone's font instead (Theme: `nominalPitch`) and the right
// column at the share of the card that the design gives it, so that the shapes do not depend on the font of the machine
// that draws them: the launch image is made on a desktop, in CI, and has to come out the same on every one.
//
// Look (Main, LogBanner): the card is at least 64 pt; the right column is 12 pt, the date in 600 and white, the
// tester in 400; the chevron is the 16 pt Icon. The strip under it is 41 pt: a full-width divider, 4 pt, a 30 pt
// row with a 16 pt warning sign and the text in 13/600, both white and centred on the row, and 6 pt.
Item {
    id: root

    required property string unitText
    required property string locationText
    required property string versionText
    required property string dateText
    required property string testerText
    required property var missingFields
    property bool skeleton: false
    // With `skeleton`: the lines and the right column from the design and not from the font (above).
    property bool nominalMetrics: false

    // The prompts that stand in for an empty field. A screen sets them when the unit prefix is not "UNIT".
    property string unitPrompt: qsTr("Unit —")
    property string locationPrompt: qsTr("Location not set")
    property string versionPrompt: qsTr("v —")
    property string testerPrompt: qsTr("Tester not set")

    // What a screen reader says: the whole card, which changes as fields are filled. A screen may replace it.
    property string accessibleName: {
        if (skeleton)
            return "";
        const unset = qsTr("not set");
        const values = [unitText.length > 0 ? unitText : unset, locationText.length > 0 ? locationText : unset, versionText.length > 0 ? versionText : unset, dateText, testerText.length > 0 ? testerText : unset];
        const spoken = qsTr("Today's details. Unit %1. Location %2. Version %3. Date %4. Tester %5.").arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]).arg(values[4]);
        const open = qsTr("Opens today's details.");
        return missingCount > 0 ? qsTr("%1 Not filled in yet: %2. %3").arg(spoken).arg(missingText).arg(open) : qsTr("%1 %2").arg(spoken).arg(open);
    }

    readonly property int missingCount: missingFields ? missingFields.length : 0
    // The empty fields by name, "UNIT number, Location, Version, Commit".
    readonly property string missingText: {
        const names = {
            "unit_number": qsTr("UNIT number"),
            "location": qsTr("Location"),
            "software_version": qsTr("Version"),
            "software_commit": qsTr("Commit")
        };
        const shown = [];
        for (let i = 0; i < missingCount; ++i)
            shown.push(names[missingFields[i]] !== undefined ? names[missingFields[i]] : missingFields[i]);
        return shown.join(qsTr(", "));
    }
    readonly property bool pressed: tap.pressed
    // From a text scale of 1.2 up the card shows two lines, the unit and the site and version, each on one line and cut with
    // an ellipsis; the date and the tester are one tap away in Today's details (Stage 7, F1). The accessible name has
    // all of it.
    readonly property bool reduced: Theme.clampedScale > 1.2 && !skeleton

    signal clicked

    implicitWidth: Theme.frame.width - 2 * Theme.space.screenMargin
    implicitHeight: main.height + (strip.visible ? strip.height : 0)
    activeFocusOnTab: true

    Accessible.ignored: root.skeleton
    Accessible.role: Accessible.Button
    Accessible.name: root.accessibleName
    Accessible.focusable: true
    Accessible.onPressAction: root.clicked()

    Rectangle {
        anchors.fill: parent
        radius: Theme.radius.headerCard
        color: root.skeleton ? Theme.colour.swatch : Theme.colour.headerCard
        border.width: root.activeFocus ? Theme.border.otherWidth : 0
        border.color: Theme.colour.textOnHeaderCard
    }

    // The unit and site on the left, the date and tester on the right, and the chevron.
    Item {
        id: main

        width: parent.width
        height: Math.max(Theme.size.headerCardHeight, Math.max(leftColumn.height, root.reduced ? 0 : rightColumn.height) + 2 * Theme.space.sectionGap)

        TextMetrics {
            id: dateMetrics

            font: Theme.font.captionSmall.selectedFont
            text: root.dateText
        }
        TextMetrics {
            id: testerMetrics

            font: Theme.font.captionSmall.font
            text: testerLabel.text
        }

        Column {
            id: leftColumn

            x: Theme.space.screenMargin
            y: (main.height - height) / 2
            width: (root.reduced ? chevron.x : rightColumn.x) - Theme.space.sectionGap - x
            spacing: Theme.border.hairline * 2

            Text {
                id: unitLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                color: Theme.colour.textOnHeaderCard
                font: Theme.font.headerUnit.font
                text: root.unitText.length > 0 ? root.unitText : root.unitPrompt
                wrapMode: Text.WordWrap
                maximumLineCount: root.reduced ? 1 : 100
                elide: root.reduced ? Text.ElideRight : Text.ElideNone
                height: root.skeleton && root.nominalMetrics ? Theme.font.headerUnit.nominalPitch : implicitHeight
                Accessible.ignored: true
            }
            Text {
                id: siteLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                color: Theme.colour.headerCardTextSecondary
                font: Theme.font.caption.font
                text: qsTr("%1 · %2").arg(root.locationText.length > 0 ? root.locationText : root.locationPrompt).arg(root.versionText.length > 0 ? qsTr("v%1").arg(root.versionText) : root.versionPrompt)
                wrapMode: Text.WordWrap
                maximumLineCount: root.reduced ? 1 : 100
                elide: root.reduced ? Text.ElideRight : Text.ElideNone
                height: root.skeleton && root.nominalMetrics ? Theme.font.caption.nominalPitch : implicitHeight
                Accessible.ignored: true
            }
        }

        Column {
            id: rightColumn

            x: chevron.x - Theme.space.sectionGap - width
            y: (main.height - height) / 2
            visible: !root.reduced
            width: root.skeleton && root.nominalMetrics ? main.width * 0.4 : Math.min(Math.ceil(Math.max(dateMetrics.advanceWidth, testerMetrics.advanceWidth)) + Theme.border.hairline, main.width * 0.4)
            spacing: Theme.border.hairline * 2

            Text {
                id: dateLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                horizontalAlignment: Text.AlignRight
                color: Theme.colour.textOnHeaderCard
                font: Theme.font.captionSmall.selectedFont
                text: root.dateText
                wrapMode: Text.WordWrap
                height: root.skeleton && root.nominalMetrics ? Theme.font.captionSmall.nominalPitch : implicitHeight
                Accessible.ignored: true
            }
            Text {
                id: testerLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                horizontalAlignment: Text.AlignRight
                color: Theme.colour.headerCardTextSecondary
                font: Theme.font.captionSmall.font
                text: root.testerText.length > 0 ? root.testerText : root.testerPrompt
                wrapMode: Text.WordWrap
                height: root.skeleton && root.nominalMetrics ? Theme.font.captionSmall.nominalPitch : implicitHeight
                Accessible.ignored: true
            }
        }

        // The launch screen's shapes, in the places of the four lines of text, which keep their room.
        Rectangle {
            visible: root.skeleton
            x: leftColumn.x
            y: leftColumn.y + unitLabel.y
            width: leftColumn.width * 0.5
            height: unitLabel.height
            radius: Theme.border.hairline * 4
            color: Theme.colour.border
        }
        Rectangle {
            visible: root.skeleton
            x: leftColumn.x
            y: leftColumn.y + siteLabel.y
            width: leftColumn.width * 0.8
            height: siteLabel.height
            radius: Theme.border.hairline * 4
            color: Theme.colour.border
        }
        Rectangle {
            visible: root.skeleton
            x: rightColumn.x
            y: rightColumn.y + dateLabel.y
            width: rightColumn.width
            height: dateLabel.height
            radius: Theme.border.hairline * 4
            color: Theme.colour.border
        }
        Rectangle {
            visible: root.skeleton
            x: rightColumn.x + rightColumn.width * 0.3
            y: rightColumn.y + testerLabel.y
            width: rightColumn.width * 0.7
            height: testerLabel.height
            radius: Theme.border.hairline * 4
            color: Theme.colour.border
        }

        Icon {
            id: chevron

            objectName: "headerChevron"
            x: main.width - Theme.space.screenMargin - width
            y: (main.height - height) / 2
            size: Theme.space.screenMargin
            visible: !root.skeleton
            kind: "chevronRight"
            colour: Theme.colour.headerCardTextSecondary
        }
    }

    // The empty fields, inside the card so that the board below does not move (docs/design/screens/LogBanner).
    Item {
        id: strip

        objectName: "headerStrip"
        // The row of the strip: 30 pt at least, between 4 pt above and 6 pt below, under the divider. At a large text size it is
        // one line with a hairline or two around it, so that the board keeps all twelve tiles above the tab bar whatever the
        // font's line height is (Stage 7, F1).
        readonly property real rowTop: Theme.border.hairline + Theme.border.hairline * (root.reduced ? 1 : 4)
        readonly property real rowHeight: Math.max(Theme.size.badge + (root.reduced ? 0 : Theme.border.hairline * 6), stripText.implicitHeight)

        y: main.height
        width: parent.width
        height: rowTop + rowHeight + Theme.border.hairline * (root.reduced ? 2 : 6) // literal-ok: hairline multiples of the strip's padding, as the row above
        visible: root.missingCount > 0 && !root.skeleton

        Rectangle {
            objectName: "headerStripDivider"
            width: parent.width
            height: Theme.border.hairline
            color: Qt.alpha(Theme.colour.textOnHeaderCard, 0.28)
        }
        Icon {
            id: stripGlyph

            x: Theme.space.screenMargin
            y: strip.rowTop + (strip.rowHeight - height) / 2
            size: Theme.space.screenMargin
            kind: "warning"
            colour: Theme.colour.textOnHeaderCard
        }
        Text {
            id: stripText

            x: stripGlyph.x + stripGlyph.width + Theme.space.tileGap
            y: strip.rowTop + (strip.rowHeight - height) / 2
            width: parent.width - x - Theme.space.screenMargin
            color: Theme.colour.textOnHeaderCard
            font: Theme.font.caption.selectedFont
            text: qsTr("Not filled in yet: %1").arg(root.missingText)
            wrapMode: Text.WordWrap
            maximumLineCount: root.reduced ? 1 : 100
            elide: root.reduced ? Text.ElideRight : Text.ElideNone
            Accessible.ignored: true
        }
    }

    TapHandler {
        id: tap

        enabled: !root.skeleton

        onTapped: root.clicked()
    }

    Keys.onReturnPressed: root.clicked()
    Keys.onSpacePressed: root.clicked()
}
