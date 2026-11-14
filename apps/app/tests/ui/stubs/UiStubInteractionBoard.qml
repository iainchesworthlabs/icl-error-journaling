// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// The model of the Interactions board (InteractionBoardModel) as the board area reads it, for the tests of the shell: it has no
// types, so the board is empty. A test sets `boardEmpty` to see the other state.
QtObject {
    property bool boardEmpty: true
}
