// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick
import ErrorJournalingApp

// The plum card at the top of the Log screen: the unit and the site and version on the left, the date and the
// facilitator on the right. A tap opens Today's details. It shows what it is given; a field that is empty reads as a
// prompt ("Location not set") and not as a gap, and the fields that are missing are listed in a strip inside the card
// (HDR-06), which carries a mark and a count so that it is not colour alone. Nothing here blocks logging.
//
// `missingFields` is `session.missingFields`, the keys of the empty fields. With `skeleton` the card draws its shapes
// only, in the neutral swatch colour and with no text, for the launch screen (Stage 5); the geometry is the same.
Item {
    id: root

    required property string unitText
    required property string locationText
    required property string versionText
    required property string dateText
    required property string facilitatorText
    required property var missingFields
    property bool skeleton: false

    // The prompts that stand in for an empty field. A screen sets them when the unit prefix is not "UNIT".
    property string unitPrompt: qsTr("Unit —")
    property string locationPrompt: qsTr("Location not set")
    property string versionPrompt: qsTr("v —")
    property string facilitatorPrompt: qsTr("Facilitator not set")

    // What a screen reader says: the whole card, which changes as fields are filled. A screen may replace it.
    property string accessibleName: {
        if (skeleton)
            return "";
        const unset = qsTr("not set");
        const values = [unitText.length > 0 ? unitText : unset, locationText.length > 0 ? locationText : unset, versionText.length > 0 ? versionText : unset, dateText, facilitatorText.length > 0 ? facilitatorText : unset];
        const spoken = qsTr("Today's details. Unit %1. Location %2. Version %3. Date %4. Facilitator %5.").arg(values[0]).arg(values[1]).arg(values[2]).arg(values[3]).arg(values[4]);
        const open = qsTr("Opens today's details.");
        return missingCount > 0 ? qsTr("%1 Not filled in yet: %2. %3").arg(spoken).arg(missingText).arg(open) : qsTr("%1 %2").arg(spoken).arg(open);
    }

    readonly property int missingCount: missingFields ? missingFields.length : 0
    // The empty fields by name, "UNIT number, Location, Version".
    readonly property string missingText: {
        const names = {
            "unit_number": qsTr("UNIT number"),
            "location": qsTr("Location"),
            "software_version": qsTr("Version")
        };
        const shown = [];
        for (let i = 0; i < missingCount; ++i)
            shown.push(names[missingFields[i]] !== undefined ? names[missingFields[i]] : missingFields[i]);
        return shown.join(qsTr(", "));
    }
    readonly property bool pressed: tap.pressed

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

    // The unit and site on the left, the date and facilitator on the right, and the chevron.
    Item {
        id: main

        width: parent.width
        height: Math.max(Theme.size.minTouchTarget + 2 * Theme.space.sectionGap, Math.max(leftColumn.height, rightColumn.height) + 2 * Theme.space.sectionGap)

        TextMetrics {
            id: dateMetrics

            font: Theme.font.sectionLabel.font
            text: root.dateText
        }
        TextMetrics {
            id: facilitatorMetrics

            font: Theme.font.caption.font
            text: facilitatorLabel.text
        }

        Column {
            id: leftColumn

            x: Theme.space.screenMargin
            y: (main.height - height) / 2
            width: rightColumn.x - Theme.space.sectionGap - x
            spacing: Theme.border.hairline * 2

            Text {
                id: unitLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                color: Theme.colour.textOnHeaderCard
                font: Theme.font.headerUnit.font
                text: root.unitText.length > 0 ? root.unitText : root.unitPrompt
                wrapMode: Text.WordWrap
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
                Accessible.ignored: true
            }
        }

        Column {
            id: rightColumn

            x: chevron.x - Theme.space.sectionGap - width
            y: (main.height - height) / 2
            width: Math.min(Math.max(dateMetrics.advanceWidth, facilitatorMetrics.advanceWidth), main.width * 0.4)
            spacing: Theme.border.hairline * 2

            Text {
                id: dateLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                horizontalAlignment: Text.AlignRight
                color: Theme.colour.textOnHeaderCard
                font: Theme.font.sectionLabel.font
                text: root.dateText
                wrapMode: Text.WordWrap
                Accessible.ignored: true
            }
            Text {
                id: facilitatorLabel

                width: parent.width
                opacity: root.skeleton ? 0 : 1
                horizontalAlignment: Text.AlignRight
                color: Theme.colour.headerCardTextSecondary
                font: Theme.font.caption.font
                text: root.facilitatorText.length > 0 ? root.facilitatorText : root.facilitatorPrompt
                wrapMode: Text.WordWrap
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
            y: rightColumn.y + facilitatorLabel.y
            width: rightColumn.width * 0.7
            height: facilitatorLabel.height
            radius: Theme.border.hairline * 4
            color: Theme.colour.border
        }

        InputGlyph {
            id: chevron

            x: main.width - Theme.space.screenMargin - width
            y: (main.height - height) / 2
            visible: !root.skeleton
            kind: "chevron"
            colour: Theme.colour.headerCardTextSecondary
        }
    }

    // The empty fields, inside the card so that the board below does not move (docs/design/screens/LogBanner).
    Item {
        id: strip

        y: main.height
        width: parent.width
        height: stripText.height + Theme.space.sectionGap
        visible: root.missingCount > 0 && !root.skeleton

        Rectangle {
            x: Theme.space.screenMargin
            width: parent.width - 2 * Theme.space.screenMargin
            height: Theme.border.hairline
            color: Qt.alpha(Theme.colour.textOnHeaderCard, 0.28)
        }
        InputGlyph {
            id: stripGlyph

            x: Theme.space.screenMargin
            y: Theme.space.tileGap
            kind: "alert"
            colour: Theme.colour.textOnHeaderCard
        }
        Text {
            id: stripText

            x: stripGlyph.x + stripGlyph.width + Theme.space.tileGap
            y: Theme.space.tileGap
            width: parent.width - x - Theme.space.screenMargin
            color: Theme.colour.textOnHeaderCard
            font: Theme.font.caption.font
            text: qsTr("Not filled in yet: %1").arg(root.missingText)
            wrapMode: Text.WordWrap
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
