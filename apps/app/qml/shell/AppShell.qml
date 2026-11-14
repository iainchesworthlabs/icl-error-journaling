// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The app shell (Stage 4, package 4.15): the four tabs (five with the second board on, below), the pages each tab pushes, the one SheetHost that sheets and alerts
// are shown in, and the routing between the screens. Everything the screens ask for by signal is answered here, and nothing
// else: a screen holds its own rules, the view models hold the data.
//
//     tab bar     Log, Review, History, Settings. Tab state is in memory and starts on Log. Tapping the tab that is
//                 selected pops it to its root.
//     second board  with `boards.interactionsEnabled` (Stage 14, package I-5; off until the person turns it on in Settings)
//                 the bar has five tabs, Errors, Interactions, Review, History and Settings, and Interactions is the
//                 Log screen again with `activeBoard` "interactions": one header card, one composer, one Last bar. The
//                 tabs have stable numbers (`logIndex` and the rest, `interactionsIndex` is 4), whatever their place in
//                 the bar, so that a switch does not move the screens. The app reopens on the board last used
//                 (`boards.lastBoard`), and turning the board off while on it goes to Errors. Without `boards` the
//                 shell is the shell of v1.
//     pages       a PageStack for each tab: Log and Review push Today's details (HeaderScreen), Settings pushes the editors
//                 of the error types, locations and note templates, and History keeps the stack of its own for the Review of a
//                 past log (`shown()` and `hidden()` are called as the tab is chosen and left). A tab is built when it is first chosen, so the Log
//                 screen is the only one that is made for the first frame.
//     sheets      one SheetHost for the window, so a sheet covers the tab bar: the Other picker, the new-day alert (NewDay,
//                 which follows `day.promptVisible`), and the screens' own sheets
//                 (the Log screen's template prompt and Edit entry sheet, and the Edit entry sheet of Review, are put in its layer). The sheets
//                 that a screen keeps inside itself (Settings, Today's details, the editors) cover that screen.
//     first run   while `settings.configured` is false, the first-launch screen (FirstLaunch) covers the tabs.
//     import      the import and export rows of Settings and FirstLaunch, and "choose another file" of the import flow, go
//                 to the file chooser (platform/<kind>/FileChooser.qml: Qt's dialog on a desktop, the document picker of
//                 Stage 5 on the phone), which answers with a path. A chosen import goes to `ImportFlow.begin`, a chosen
//                 export to `settings.exportTo`.
//     messages    `AppRoot.writeFailed` is one Banner at the top of the window, except on the Log screen, which shows
//                 the failures of its own models in its own banner. The Undo toast is the Log screen's; Review's is here,
//                 because the Review screen has none.
//     theme       `Theme.textScale` and `Theme.reduceMotion` follow the bridge (Dynamic Type, Reduce Motion), and
//                 `WindowLayout.windowWidth` and `WindowLayout.windowHeight` follow the shell's own size (Stage 8).
//
// Given as `required property`, the nine services of AppRoot by their names, AppRoot itself and the bridge (decision 3), so
// a missing one fails when the shell is made and not on a phone.
//
// What Stage 5 keeps (5.7) is in C++ and in `platform/ios/FileChooser.qml`: the launch tracker (`boardEnabled` is when the
// 12 tiles accept taps), the document picker, and the device pass. The day check on becoming active and the haptic
// policy's active state are done by the lifecycle of `main.cpp`, not here.
Item {
    id: root

    required property var session
    required property var undo
    required property var board
    required property var picker
    required property var timeline
    required property var history
    required property var day
    required property var settings
    required property var share
    // AppRoot, for its `writeFailed(operation, message)`.
    required property var appRoot
    // The version and platform text, at the foot of Settings.
    property string aboutText
    // The AboutModel, for the About pages behind the last row of Settings (PRF-12), which also show the timing line behind five
    // taps on the Version row. Null leaves the row out.
    property var about: null
    // What the in-app help remembers (HelpState), for the first-run tip of the Log screen. Null shows no tip.
    property var help: null
    // Which boards there are and which was used last (BoardState): `interactionsEnabled`, `lastBoard`. Null is the app of v1: four
    // tabs and one board.
    property var boards: null
    // What the Interactions board shows (InteractionBoardModel): `boardEmpty`. Null reads as an empty board.
    property var interactionBoard: null
    // The list of people (PeopleModel), for the picker of the Interactions board. Null: no picker.
    property var people: null
    // The QmlBridge: the haptics, the share sheet and the accessibility settings.
    required property var bridge
    // The file chooser that import and export go through. The platform's own unless the test of the shell gives it another.
    property var fileChooser: platformChooser
    // What opens an address in the browser (the source code row of the About page): `open(url)`. The system's unless the test of
    // the shell gives it another, so that a test opens no browser.
    property var urlOpener: platformUrlOpener

    // The tabs, in the order of the tab bar.
    readonly property int logIndex: 0
    readonly property int reviewIndex: 1
    readonly property int historyIndex: 2
    readonly property int settingsIndex: 3
    // The fifth tab, the Interactions board. It is shown only with the second board on, and is fifth in number whatever its place.
    readonly property int interactionsIndex: 4
    // The second board is on: the bar has five tabs.
    readonly property bool interactionsOn: boards !== null && boards.interactionsEnabled

    // 5.7: the board is on screen and can be tapped (the Log tab is shown, the session has loaded, the first-launch screen is
    // not covering it). The launch tracker is fed from this, and `board_enabled()` stays with it.
    readonly property bool boardEnabled: logScreen.visible && !logScreen.loading

    // The tab that is shown, by its number (not its place in the bar). The bar follows it.
    property int currentTab: logIndex
    // Where a tab is in the bar: the first tab, Errors, Interactions when it is on, then Review, History and Settings.
    function positionOf(tab: int): int {
        if (!interactionsOn)
            return tab === interactionsIndex ? logIndex : tab;
        // Errors, Interactions, Review, History, Settings.
        switch (tab) {
        case interactionsIndex:
            return 1;
        case reviewIndex:
            return 2;
        case historyIndex:
            return 3;
        case settingsIndex:
            return 4;
        default:
            return 0;
        }
    }

    function tabAt(position: int): int {
        if (!interactionsOn)
            return position;
        switch (position) {
        case 1:
            return interactionsIndex;
        case 2:
            return reviewIndex;
        case 3:
            return historyIndex;
        case 4:
            return settingsIndex;
        default:
            return logIndex;
        }
    }

    // The Log screen holds both boards.
    function isBoardTab(tab: int): bool {
        return tab === logIndex || tab === interactionsIndex;
    }
    // The screens that a Loader makes, null until their tab has been chosen once.
    readonly property var reviewStack: reviewTab.item
    readonly property var historyScreen: historyTab.item
    readonly property var settingsScreen: settingsTab.item
    // Review is in view, on its tab or as the past log of History, and nothing covers it.
    readonly property bool reviewToastWanted: !detailsOpen && (currentTab === reviewIndex || (currentTab === historyIndex && historyScreen !== null && historyScreen.reviewShown))
    // The tabs that have been shown, one bit for each, so that a tab is built on its first visit.
    property int visitedTabs: 1
    property int previousTab: 0
    // Today's details cover the tab bar, as the design draws them; the tab bar is back when the page closes.
    property bool detailsOpen: false
    // The first-launch screen is up: the settings have loaded and there is no setup yet.
    readonly property bool firstLaunchShown: !settings.configured && !settings.loading
    // The layer of the window's SheetHost, for the sheets that other screens keep: Log's template prompt, Review's edit sheet.
    readonly property Item sheetLayer: findLayer(windowHost)
    // What a failed write says, and whether it is on screen.
    property string failureMessage: ""
    readonly property bool failureShown: failureMessage.length > 0

    function wasVisited(tab: int): bool {
        return (visitedTabs & (1 << tab)) !== 0;
    }

    // The item of a SheetHost that holds its sheets.
    function findLayer(host: Item): Item {
        for (let i = 0; i < host.children.length; ++i) {
            if (host.children[i].objectName === "sheetHostLayer")
                return host.children[i];
        }
        return host;
    }

    // The Log screen shows the failures of its own models in a banner of its own, so the window's banner is for the rest.
    function logShowsFailures(): bool {
        return isBoardTab(currentTab) && logStack.depth === 1 && !firstLaunchShown;
    }

    // Today's details, on the stack of the tab that asked.
    function showDetails(stack: var): void {
        if (detailsOpen)
            return;
        detailsOpen = true;
        stack.push(detailsPage);
    }

    // The help sheet of a topic (HelpTopics), over whatever is on screen.
    function showHelp(topic: string): void {
        helpSheet.show(topic);
    }

    function selectTab(index: int): void {
        // The Interactions tab is not there while the second board is off.
        currentTab = index === interactionsIndex && !interactionsOn ? logIndex : index;
    }

    // The board setup in Settings, from the Log screen's empty board or an empty tile: the Settings list, and the picker
    // for the tile at `position` (1 to 11) when there is one.
    function showBoardSetup(position: int): void {
        selectTab(settingsIndex);
        // The Settings tab is made when it is first shown.
        Qt.callLater(() => {
            if (!settingsScreen)
                return;
            settingsScreen.popToRoot();
            if (position > 0)
                settingsScreen.addTypeAt(position);
        });
    }

    // The keyboard is up and no text field has the focus: it is put away. Pages and sheets that have just closed move the
    // focus to a screen or to nothing, and the platform's keyboard does not always follow (it stayed over the Log screen after
    // Today's details was left); this is the catch-all, and the PageStack, the Sheet and the tab bar dismiss it themselves.
    function checkKeyboard(): void {
        if (Keyboard.visible && Keyboard.focusedInput(root) === null)
            Keyboard.hide();
    }

    // The tab that was tapped again goes back to its first page.
    function popToRoot(tab: int): void {
        if (isBoardTab(tab))
            logStack.pop(null);
        else if (tab === reviewIndex && reviewStack)
            root.reviewStack.pop(null);
        else if (tab === historyIndex && historyScreen) {
            historyScreen.hidden();
            historyScreen.shown();
        } else if (tab === settingsIndex && settingsScreen)
            settingsScreen.popToRoot();
    }

    implicitWidth: Theme.frame.width
    implicitHeight: Theme.frame.height

    // History reads its list when its tab is shown, and lets the open log go when the tab is left. The first time, the
    // screen reads it itself when it has been made (`onLoaded` of its Loader).
    onCurrentTabChanged: {
        // The field that was being typed in is on a tab that is no longer shown.
        Keyboard.dismiss(root, null);
        const historyMade = historyScreen !== null;
        if (historyMade && previousTab === historyIndex)
            historyScreen.hidden();
        visitedTabs |= (1 << currentTab);
        if (historyMade && currentTab === historyIndex)
            historyScreen.shown();
        previousTab = currentTab;
        // The app reopens on the board that was used last.
        if (boards !== null && isBoardTab(currentTab))
            boards.lastBoard = currentTab === interactionsIndex ? "interactions" : "errors";
    }
    Component.onCompleted: {
        if (interactionsOn && boards.lastBoard === "interactions")
            currentTab = interactionsIndex;
    }
    onFailureMessageChanged: {
        if (failureMessage.length > 0)
            Accessible.announce(qsTr("Not saved. %1").arg(failureMessage));
    }

    // Dynamic Type and Reduce Motion: the theme follows the system through the bridge (Stage 4, 4.3).
    Binding {
        target: Theme
        property: "textScale"
        value: root.bridge.textScale
    }

    Binding {
        target: Theme
        property: "reduceMotion"
        value: root.bridge.reduceMotion
    }

    // The layout class follows the shell's size (Stage 8, 8.1): the window on the phone, the window or the Split View or
    // Stage Manager window on an iPad, the resizable window on a desktop.
    Binding {
        target: WindowLayout
        property: "windowWidth"
        value: root.width
        when: root.width > 0
    }

    Binding {
        target: WindowLayout
        property: "windowHeight"
        value: root.height
        when: root.height > 0
    }

    Connections {
        function onActiveFocusItemChanged(): void {
            Qt.callLater(root.checkKeyboard);
        }

        target: root.Window.window
    }

    // The bar follows the tab that is shown, and the tab follows a tap on the bar (`onTabSelected` below).
    Binding {
        target: tabBar
        property: "currentIndex"
        value: root.positionOf(root.currentTab)
        restoreMode: Binding.RestoreNone
    }

    // The second board was turned off on the board itself (or by a setup file): the app goes to Errors, and keeps everything.
    Connections {
        function onInteractionsEnabledChanged(): void {
            if (!root.interactionsOn && root.currentTab === root.interactionsIndex)
                root.currentTab = root.logIndex;
        }

        target: root.boards
        enabled: root.boards !== null
    }

    Connections {
        function onWriteFailed(operation: string, message: string): void {
            if (!root.logShowsFailures())
                root.failureMessage = message;
        }

        target: root.appRoot
    }

    SheetHost {
        id: windowHost

        anchors.fill: parent

        Rectangle {
            anchors.fill: parent
            color: Theme.colour.background
        }

        // ---- the tabs ----

        Item {
            id: tabs

            objectName: "shellTabs"
            anchors.fill: parent
            visible: !root.firstLaunchShown
            // The import flow covers the tabs, and the tabs are unavailable under it.
            enabled: !importFlow.active

            Item {
                id: logTab

                objectName: "logTab"
                anchors.fill: parent
                visible: root.isBoardTab(root.currentTab)

                PageStack {
                    id: logStack

                    anchors.fill: parent
                    accessibleName: qsTr("Log pages")
                    initialItem: logScreen
                }

                LogScreen {
                    id: logScreen

                    objectName: "logScreen"
                    session: root.session
                    board: root.board
                    undo: root.undo
                    timeline: root.timeline
                    settings: root.settings
                    bridge: root.bridge
                    dictation: root.bridge
                    picker: root.picker
                    share: root.share
                    help: root.help
                    bottomClearance: tabBar.height
                    sheetLayer: root.sheetLayer
                    activeBoard: root.currentTab === root.interactionsIndex ? "interactions" : "errors"
                    interactionBoard: root.interactionBoard
                    people: root.people
                    onInteractionSetupRequested: root.showBoardSetup(0)
                    onBoardSwitchRequested: board => root.selectTab(board === "interactions" ? root.interactionsIndex : root.logIndex)
                    onTodaysDetailsRequested: root.showDetails(logStack)
                    onReviewRequested: root.selectTab(root.reviewIndex)
                    onOtherRequested: otherPicker.open()
                    onBoardSetupRequested: position => root.showBoardSetup(position)
                }
            }

            Loader {
                id: reviewTab

                objectName: "reviewTab"
                anchors.fill: parent
                active: root.wasVisited(root.reviewIndex)
                visible: root.currentTab === root.reviewIndex

                sourceComponent: PageStack {
                    accessibleName: qsTr("Review pages")
                    initialItem: reviewPage
                }
            }

            Loader {
                id: historyTab

                objectName: "historyTab"
                anchors.fill: parent
                active: root.wasVisited(root.historyIndex)
                visible: root.currentTab === root.historyIndex

                // History has a stack of its own, for the Review of a past log.
                sourceComponent: History {
                    objectName: "historyScreen"
                    history: root.history
                    share: root.share
                    picker: root.picker
                    settings: root.settings
                    bottomClearance: tabBar.height
                    // The delete alert and the sheets of a past log cover the tab bar, as every sheet does.
                    sheetLayer: root.sheetLayer
                }
                onLoaded: root.historyScreen.shown()
            }

            Loader {
                id: settingsTab

                objectName: "settingsTab"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.bottomMargin: tabBar.height
                active: root.wasVisited(root.settingsIndex)
                visible: root.currentTab === root.settingsIndex

                sourceComponent: SettingsScreen {
                    objectName: "settingsScreen"
                    settings: root.settings
                    boards: root.boards
                    aboutText: root.aboutText
                    about: root.about
                    timingText: root.bridge.timingSummary
                    onErrorTypesRequested: pushPage(errorTypesPage, {})
                    onTemplatesRequested: pushPage(templatesPage, {})
                    onLocationsRequested: pushPage(locationsPage, {})
                    onUnitEnvironmentsRequested: pushPage(unitEnvironmentsPage, {})
                    onOpenUrlRequested: url => root.urlOpener.open(url)
                    onHelpRequested: topic => root.showHelp(topic)
                    onImportRequested: root.fileChooser.chooseToOpen()
                    onExportRequested: root.fileChooser.chooseToSave(SetupFile.defaultFileName)
                }
            }

            // Review's Undo toast, for Review and for the Review of a past log in History; the Log screen has its own.
            UndoToast {
                id: reviewToast

                objectName: "reviewToast"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.leftMargin: Theme.space.screenMargin
                anchors.rightMargin: Theme.space.screenMargin
                anchors.bottomMargin: tabBar.height + Theme.space.sectionGap
                active: root.undo.active && root.reviewToastWanted
                text: root.undo.text
                undoing: root.undo.undoing
                generation: root.undo.generation
                windowMs: root.undo.windowMs
                onUndoRequested: root.undo.undo()
            }
        }

        // ---- messages ----

        SafeFrame {
            id: safe

            anchors.fill: parent
            visible: !root.firstLaunchShown

            Banner {
                objectName: "writeFailedBanner"
                x: Theme.space.screenMargin
                width: parent.width - 2 * Theme.space.screenMargin
                z: 1
                visible: root.failureShown
                tone: Banner.Error
                title: qsTr("Not saved")
                message: root.failureMessage
                maximumHeight: root.height / 2
                onCloseRequested: root.failureMessage = ""
            }
        }

        TabBar {
            id: tabBar

            objectName: "shellTabBar"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            bottomInset: safe.bottomInset
            visible: !root.detailsOpen && !root.firstLaunchShown
            interactionsEnabled: root.interactionsOn
            onTabSelected: index => root.currentTab = root.tabAt(index)
            onTabReselected: index => root.popToRoot(root.tabAt(index))
        }

        // ---- first launch and the import ----

        Loader {
            id: firstLaunch

            objectName: "firstLaunch"
            anchors.fill: parent
            active: root.firstLaunchShown

            sourceComponent: FirstLaunch {
                settings: root.settings
                onImportRequested: root.fileChooser.chooseToOpen()
            }
        }

        // It covers the whole window, the first-launch screen and the tab bar included, and shows nothing until a file has
        // been read.
        ImportFlow {
            id: importFlow

            objectName: "importFlow"
            anchors.fill: parent
            settings: root.settings
            onImportRequested: root.fileChooser.chooseToOpen()
            onHelpRequested: topic => root.showHelp(topic)
            onGuideRequested: url => root.urlOpener.open(url)
        }

        QtObject {
            id: platformUrlOpener

            function open(url: string): void {
                Qt.openUrlExternally(url);
            }
        }

        FileChooser {
            id: platformChooser

            objectName: "fileChooser"
        }

        Connections {
            function onOpenChosen(path: string, displayName: string): void {
                importFlow.begin(path, displayName);
            }

            function onSaveChosen(path: string): void {
                root.settings.exportTo(path);
            }

            // 5.7: a setup file that could not be used, or an export that could not be prepared (iPhone build).
            function onFailed(message: string): void {
                root.failureMessage = message;
            }

            target: root.fileChooser
        }

        // The new-day alert is after the Other picker, so that it is over everything. It follows `day.promptVisible`.
        sheets: [
            OtherPicker {
                id: otherPicker

                objectName: "otherPicker"
                picker: root.picker
                session: root.session
                bridge: root.bridge
            },
            // The help of a screen (Stage 10, 10.6). Over the tab bar and the import flow, under the new-day alert.
            HelpSheet {
                id: helpSheet

                objectName: "helpSheet"
                onGuideRequested: url => root.urlOpener.open(url)
            },
            NewDay {
                objectName: "newDay"
                day: root.day
            }
        ]
    }

    // ---- pages ----

    // Today's details, over the tab bar. "Start logging" closes it.
    Component {
        id: detailsPage

        HeaderScreen {
            objectName: "headerScreen"
            session: root.session
            settings: root.settings
            timeline: root.timeline
            share: root.share
            onCloseRequested: {
                root.detailsOpen = false;
                StackView.view.pop();
            }
            onHelpRequested: topic => root.showHelp(topic)
        }
    }

    Component {
        id: reviewPage

        Review {
            objectName: "reviewScreen"
            timeline: root.timeline
            share: root.share
            picker: root.picker
            settings: root.settings
            bottomClearance: tabBar.height
            onHeaderRequested: root.showDetails(StackView.view)
            onHelpRequested: topic => root.showHelp(topic)
            // The edit sheet and its time sheet cover the tab bar, as every sheet does.
            sheetLayer: root.sheetLayer
        }
    }

    Component {
        id: errorTypesPage

        ErrorTypes {
            settings: root.settings
            // The sheet of the editor covers the tab bar, as every sheet does.
            sheetLayer: root.sheetLayer
            onCloseRequested: StackView.view.pop()
            onHelpRequested: topic => root.showHelp(topic)
        }
    }

    Component {
        id: locationsPage

        Locations {
            settings: root.settings
            // The sheet of the editor covers the tab bar, as every sheet does.
            sheetLayer: root.sheetLayer
            onCloseRequested: StackView.view.pop()
        }
    }

    Component {
        id: unitEnvironmentsPage

        UnitEnvironments {
            settings: root.settings
            // The sheet of the editor covers the tab bar, as every sheet does.
            sheetLayer: root.sheetLayer
            onCloseRequested: StackView.view.pop()
        }
    }

    Component {
        id: templatesPage

        Templates {
            settings: root.settings
            onCloseRequested: StackView.view.pop()
            onHelpRequested: topic => root.showHelp(topic)
        }
    }
}
