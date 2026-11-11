// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/document_picker.hpp"
#include "error_journaling/document_types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace ej::bridge
{

/// The decisions around opening a setup file on Android, in portable C++ so that a test covers them on the desktop and
/// the Java (EjDocuments.java) only moves bytes (docs/plan/stage-9.md, "File picker and open-with").

/// Android hands the application a content:// stream, not a file. It is copied into a private directory of the
/// temporary space, and the importer (ConfigImporter) takes it from there as a local file and deletes it on every
/// outcome. The launch purge (purge_import_leftovers) empties this directory, for a copy that nothing took.
inline constexpr std::string_view android_import_directory_name = "ej-import";

/// How many bytes of a stream are copied: one more than the importer accepts, so that a file that is too large is
/// refused by the importer with its own message ("That file is too large to be a setup file.") and a stream that never
/// ends does not fill the storage.
inline constexpr std::uint64_t android_import_copy_cap_bytes = CONFIG_MAX_BYTES + 1;

/// The types the document picker offers. A `.ejconfig` file has no registered type, and document providers report it as
/// `application/octet-stream`; the setup file is JSON, and some providers name it `text/plain`. The content is checked by
/// the importer, never the type or the name.
inline constexpr std::array<std::string_view, 3> android_picker_mime_types = {
    "application/json", "application/octet-stream", "text/plain"};

/// The integers `EjDocuments.java` passes to `nativePicked`: 0 a file was copied, 1 the person dismissed the picker.
/// Anything else, including a number this code does not know, is a failure.
[[nodiscard]] PickOutcome android_pick_outcome_from_code(int code) noexcept;

} // namespace ej::bridge
