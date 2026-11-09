// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// What opens an address in the browser, for the test of the shell (app_flow_test.cpp): the real one would start a browser on a
// desktop. It has the one call of the shell's own (`open(url)`) and keeps what it was asked.
Item {
    id: root

    property var opened: []

    function open(url: string): void {
        root.opened = root.opened.concat([url]);
    }
}
