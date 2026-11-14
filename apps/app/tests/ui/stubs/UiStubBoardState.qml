// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// Which boards there are and which was used last (BoardState), for the tests of the shell and of Settings. It has the names and
// the rules of the real one: the second board is off, the last board is "errors" or "interactions", the second board cannot be
// the last while it is off, and turning it off on that board goes back to Errors. A test makes a "restart" by setting the two
// properties as the real state would have them after one.
QtObject {
    id: root

    property bool interactionsEnabled: false
    property string lastBoard: "errors"

    onInteractionsEnabledChanged: {
        if (!interactionsEnabled && lastBoard === "interactions")
            lastBoard = "errors";
    }
    onLastBoardChanged: {
        if (lastBoard === "interactions" && !interactionsEnabled)
            lastBoard = "errors";
    }
}
