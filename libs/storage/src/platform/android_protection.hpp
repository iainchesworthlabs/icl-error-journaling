// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "file_protection.hpp"

#include <filesystem>

namespace ej::platform
{

/// Which data protection class Android gives a file by where it is (docs/plan/stage-9.md, "File protection and storage
/// location"). Android encrypts the app's internal storage per file; by default it is credential-encrypted storage, which
/// can be read only after the person has unlocked the phone once since it started. That is the guarantee of iOS's
/// NSFileProtectionCompleteUntilFirstUserAuthentication, and it needs no call: it applies to every file under
/// `/data/user/<user>/<package>/` (also reached as `/data/data/<package>/`), which is where `files/` (the store) and
/// `cache/` (the share and import copies) are.
///
/// - COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION: a path inside the credential-encrypted storage of an app.
/// - NONE: a path inside device-encrypted storage (`/data/user_de/<user>/<package>/`), readable before the first unlock.
///   The application does not use it (it is not direct-boot aware).
/// - OTHER: anything else: shared storage, `/tmp`, a relative path, or a path with `..` left in it after normalising.
///   The path is normalised first (`.`, `..` and doubled slashes), so a path that climbs out of the app's storage lands
///   outside the prefixes above and is OTHER.
///
/// Portable, so that the table is tested in CI; only the Android FileProtection calls it.
[[nodiscard]] ProtectionClass android_protection_class(const std::filesystem::path& path);

} // namespace ej::platform
