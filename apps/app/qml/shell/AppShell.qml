// SPDX-License-Identifier: GPL-3.0-or-later
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic
import ErrorJournalingApp

// The app shell (Stage 4, package 4.15): the four tabs, the pages each tab pushes, the one SheetHost that sheets and alerts
// are shown in, and the routing between the screens. Everything the screens ask for by signal is answered here, and nothing
// else: a screen holds its own rules, the view models hold the data.
//
//     tab bar     Log, Review, History, Settings. Tab state is in memory and starts on Log. Tapping the tab that is
//                 selected pops it to its root.
//     pages       a PageStack for each tab: Log and Review push Today's details (HeaderScreen), Settings pushes the editors
//                 of the error types, locations and note templates. A tab is built when it is first chosen, so the Log
//                 screen is the only one that is made for the first frame.
//     sheets      one SheetHost for the window, so a sheet covers the tab bar: the Other picker, and the screens' own sheets
//                 (the Log screen's template prompt and the Edit entry sheet of Review are put in its layer). The sheets
//                 that a screen keeps inside itself (Settings, Today's details, the editors) cover that screen.
//     first run   while `settings.configured` is false, the first-launch screen (FirstLaunch) covers the tabs.
//     import      the import and export rows of Settings and FirstLaunch, and "choose another file" of the import flow, go
//                 to the file chooser (platform/<kind>/FileChooser.qml: Qt's dialog on a desktop, the document picker of
//                 Stage 5 on the phone), which answers with a path. A chosen import goes to `ImportFlow.begin`, a chosen
//                 export to `settings.exportTo`.
//     messages    `AppRoot.writeFailed` is one Banner at the top of the window, except on the Log screen, which shows
//                 the failures of its own models in its own banner. The Undo toast is the Log screen's; Review's is here,
//                 because the Review screen has none.
//     theme       `Theme.textScale` and `Theme.reduceMotion` follow the bridge (Dynamic Type, Reduce Motion).
//
// Given as `required property`, the nine services of AppRoot by their names, AppRoot itself and the bridge (decision 3), so
// a missing one fails when the shell is made and not on a phone.
//
// What Stage 5 keeps (5.7) is in C++ and in `platform/ios/FileChooser.qml`: the launch tracker (`boardEnabled` is when the
// 12 tiles accept taps), the document picker, and the device pass. The day check on becoming active and the haptic
// policy's active state are done by the lifecycle of `main.cpp`, not here.
// Left for package 4.11 (new day and History): the new-day alert in `sheets`, and the History tab (`historyPage`).
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
    // The QmlBridge: the haptics, the share sheet and the accessibility settings.
    required property var bridge
    // The file chooser that import and export go through. The platform's own unless the test of the shell gives it another.
    property var fileChooser: platformChooser

    // The tabs, in the order of the tab bar.
    readonly property int logIndex: 0
    readonly property int reviewIndex: 1
    readonly property int historyIndex: 2
    readonly property int settingsIndex: 3

    // 5.7: the board is on screen and can be tapped (the Log tab is shown, the session has loaded, the first-launch screen is
    // not covering it). The launch tracker is fed from this, and `board_enabled()` stays with it.
    readonly property bool boardEnabled: logScreen.visible && !logScreen.loading

    readonly property int currentTab: tabBar.currentIndex
    // The screens that a Loader makes, null until their tab has been chosen once.
    readonly property var reviewStack: reviewTab.item
    readonly property var historyStack: historyTab.item
    readonly property var settingsScreen: settingsTab.item
    // Review is in view and nothing covers it.
    readonly property bool reviewToastWanted: !detailsOpen && currentTab === reviewIndex
    // The tabs that have been shown, one bit for each, so that a tab is built on its first visit.
    property int visitedTabs: 1
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
        return currentTab === logIndex && logStack.depth === 1 && !firstLaunchShown;
    }

    // Today's details, on the stack of the tab that asked.
    function showDetails(stack: var): void {
        if (detailsOpen)
            return;
        detailsOpen = true;
        stack.push(detailsPage);
    }

    function selectTab(index: int): void {
        tabBar.currentIndex = index;
    }

    // The tab that was tapped again goes back to its first page.
    function popToRoot(tab: int): void {
        if (tab === logIndex)
            logStack.pop(null);
        else if (tab === reviewIndex && reviewStack)
            root.reviewStack.pop(null);
        else if (tab === historyIndex && historyStack)
            historyStack.pop(null);
        else if (tab === settingsIndex && settingsScreen)
            settingsScreen.popToRoot();
    }

    implicitWidth: Theme.frame.width
    implicitHeight: Theme.frame.height

    onCurrentTabChanged: visitedTabs |= (1 << currentTab)
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
                visible: root.currentTab === root.logIndex

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
                    bottomClearance: tabBar.height
                    sheetLayer: root.sheetLayer
                    onTodaysDetailsRequested: root.showDetails(logStack)
                    onReviewRequested: root.selectTab(root.reviewIndex)
                    onOtherRequested: otherPicker.open()
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

                // 4.11: History (a grouped list of the past logs, with the Review of a past log on a stack of its own)
                // replaces this page, and `shown()` and `hidden()` are called as the tab is chosen and left.
                sourceComponent: PageStack {
                    accessibleName: qsTr("History pages")
                    initialItem: historyPage
                }
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
                    onErrorTypesRequested: pushPage(errorTypesPage, {})
                    onTemplatesRequested: pushPage(templatesPage, {})
                    onLocationsRequested: pushPage(locationsPage, {})
                    onImportRequested: root.fileChooser.chooseToOpen()
                    onExportRequested: root.fileChooser.chooseToSave(SetupFile.defaultFileName)
                }
            }

            // Review's Undo toast; the Log screen has its own.
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
            onTabReselected: index => root.popToRoot(index)
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

        // 4.11: the new-day alert (NewDay, bound to `day`) goes here, after the Other picker, so that it is over everything.
        sheets: [
            OtherPicker {
                id: otherPicker

                objectName: "otherPicker"
                picker: root.picker
                session: root.session
                bridge: root.bridge
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
            onCloseRequested: {
                root.detailsOpen = false;
                StackView.view.pop();
            }
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
            // The edit sheet covers the tab bar, as every sheet does.
            Component.onCompleted: editor.parent = root.sheetLayer
        }
    }

    // 4.11: History replaces this.
    Component {
        id: historyPage

        HistoryPlaceholder {}
    }

    Component {
        id: errorTypesPage

        ErrorTypes {
            settings: root.settings
            onCloseRequested: StackView.view.pop()
        }
    }

    Component {
        id: locationsPage

        Locations {
            settings: root.settings
            onCloseRequested: StackView.view.pop()
        }
    }

    Component {
        id: templatesPage

        Templates {
            settings: root.settings
            onCloseRequested: StackView.view.pop()
        }
    }
}
