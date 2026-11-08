// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import ErrorJournalingApp

// The Settings screen (Stage 4, package 4.12; PRF-01 to 07, 10): the miniature board on which the facilitator arranges the
// tiles, and the list of the rest of the setup. It is the root of the Settings tab, placed by the shell above the tab bar,
// and it takes the one model it shows:
//
//   settings   the SettingsModel (properties and invokables as in docs/plan/stage-3.md; `boardLayout` is the model of the
//              miniature board)
//
// What it does itself:
//   the board        a hold and drag, or Move earlier and Move later, calls `settings.moveBoardTile(from, to)`; Replace type
//                    opens a sheet of the error types and a choice calls `settings.assignBoardTile(position, typeId)`.
//                    Positions are 1 to 11 and Other is 12. The page does not scroll while a tile is being dragged.
//   the pages        the facilitator, the time format and the import and export page are pushed on the screen's own
//                    stack, with a Back in the title bar. The facilitator page also holds "In the shared log": the row
//                    of the unit prefix, which opens a page of its own, and the totals switch. The Settings mock-up has
//                    no row for either, so they are there and not on the list.
//   the refusals     while a save is in flight the model refuses another change, and says so in words; a refusal, and a
//                    write that failed, show inline. While `settings.loading` the page shows its skeleton.
//
// What it leaves to the shell, as signals (the editors are package 4.13, the file dialog is the platform's):
//
//   errorTypesRequested, templatesRequested, locationsRequested   open the editor of that list
//   importRequested, exportRequested                              choose a file, then `settings.loadImport(path)` or
//                                                                 `settings.exportTo(path)`
//
// The shell may show an editor on the screen's stack with `pushPage` (the Back of the title bar then returns to the list),
// and goes back to the list with `popToRoot` when the Settings tab is tapped again. The sheet of this screen covers the
// screen and not the tab bar.
Item {
    id: root

    required property var settings
    // The footer of the list: the version and platform text, and the timing line behind five taps on it (Stage 4, 4.15).
    property string aboutText
    property string timingText

    signal errorTypesRequested
    signal templatesRequested
    signal locationsRequested
    signal importRequested
    signal exportRequested

    // The safe-area insets, for a test: the platform's are used unless `overrideInsets` is set.
    property alias overrideInsets: frame.overrideInsets
    property alias overrideTop: frame.overrideTop
    property alias overrideBottom: frame.overrideBottom
    property alias overrideLeft: frame.overrideLeft
    property alias overrideRight: frame.overrideRight
    readonly property real topInset: frame.topInset
    // How many pages are on the stack: 1 is the list.
    readonly property int depth: stack.depth

    // Shows a page of the shell's on the stack of this screen. `component` is a Component whose root fills the stack.
    function pushPage(component: Component, properties: var): void {
        stack.push(component, properties);
    }

    function popPage(): void {
        stack.pop();
    }

    function popToRoot(): void {
        stack.pop(null);
    }

    SafeFrame {
        id: frame

        anchors.fill: parent
    }

    SheetHost {
        anchors.fill: parent

        PageStack {
            id: stack

            anchors.fill: parent
            accessibleName: qsTr("Settings pages")
            initialItem: listPage
        }

        sheets: [
            SettingsTypePicker {
                id: picker

                settings: root.settings
                overrideInsets: root.overrideInsets
                overrideTop: root.overrideTop
                overrideBottom: root.overrideBottom
                overrideLeft: root.overrideLeft
                overrideRight: root.overrideRight
            }
        ]
    }

    Component {
        id: listPage

        SettingsList {
            settings: root.settings
            aboutText: root.aboutText
            timingText: root.timingText
            topInset: frame.topInset
            onErrorTypesRequested: root.errorTypesRequested()
            onTemplatesRequested: root.templatesRequested()
            onLocationsRequested: root.locationsRequested()
            onFacilitatorRequested: stack.push(textPage, {
                "kind": "facilitator"
            })
            onTimeFormatRequested: stack.push(timeFormatPage)
            onImportExportRequested: stack.push(importExportPage)
            onReplaceRequested: (position, typeId) => picker.openFor(position, typeId)
        }
    }

    Component {
        id: textPage

        SettingsTextPage {
            settings: root.settings
            topInset: frame.topInset
            onCloseRequested: stack.pop()
            onUnitPrefixRequested: stack.push(textPage, {
                "kind": "unitPrefix"
            })
        }
    }

    Component {
        id: timeFormatPage

        SettingsTimeFormatPage {
            settings: root.settings
            topInset: frame.topInset
            onCloseRequested: stack.pop()
        }
    }

    Component {
        id: importExportPage

        SettingsImportExportPage {
            settings: root.settings
            topInset: frame.topInset
            onCloseRequested: stack.pop()
            onImportRequested: root.importRequested()
            onExportRequested: root.exportRequested()
        }
    }
}
