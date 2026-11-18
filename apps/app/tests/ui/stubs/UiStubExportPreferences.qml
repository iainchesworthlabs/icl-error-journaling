// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick

// How shared text is written (ExportPreferences), for the tests of Settings. The level of a name is "initials" until a test or the
// page sets it to "full" or "hidden"; any other word is ignored, as the real one does.
QtObject {
    id: root

    property string nameExport: "initials"
    property string interactionMarker: "Interaction"

    onNameExportChanged: {
        if (nameExport !== "full" && nameExport !== "initials" && nameExport !== "hidden")
            nameExport = "initials";
    }
}
