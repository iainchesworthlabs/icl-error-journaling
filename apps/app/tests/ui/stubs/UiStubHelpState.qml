// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// What the in-app help remembers (HelpState), for the tests of the shell and the Log screen (help_test.cpp): the first-run
// tip is open until it is closed. It has the names of the real one (`tipOpen`, `noteTipShown()`, `closeTip()`) and counts
// the calls. A test makes a "restart" by setting `tipOpen` as the real state would have it after one.
QtObject {
    id: root

    property bool tipOpen: true
    property int shownCalls: 0
    property int closeCalls: 0

    function noteTipShown(): void {
        root.shownCalls += 1;
    }

    function closeTip(): void {
        root.closeCalls += 1;
        root.tipOpen = false;
    }
}
