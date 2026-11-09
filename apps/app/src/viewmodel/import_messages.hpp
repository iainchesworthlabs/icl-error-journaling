// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"

#include <QString>

namespace ej::viewmodel
{

/// One problem found in a setup file, as a plain sentence for the person who is importing it (ImportErrors.dc.html):
/// "Error type 3 has no name.", "Locations 1 and 2 are both called North Site.", "Note template 1: the line in the log
/// has no {value} where the answer goes.". The numbers are places in the file's lists, counting from 1, in the order
/// the person sees them.
///
/// It reads the structured fields of `ConfigError` (the section, the item, the field, the kind of problem) and never
/// shows a path of the file. `ConfigError::m_Message` stays the wording for a person who edits the file by hand. Every
/// sentence goes through `tr`, so the app can be translated. Never empty.
[[nodiscard]] QString plain_import_message(const ConfigError& error);

} // namespace ej::viewmodel
