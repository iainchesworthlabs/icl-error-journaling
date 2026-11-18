// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

namespace ej::bridge
{

/// The application's own file type for the setup (configuration) file, as the property list declares it
/// (apps/app/packaging/ios/Info.plist.in). tools/ci/check_ios_plist.py reads these constants and fails when the
/// property list differs, so there is one place to change.
///
/// The identifier is the bundle identifier plus ".config"; the type conforms to public.data and public.content and not to
/// public.json, which the system previews instead of opening the application from AirDrop. A plain ".json" file is
/// accepted as well (the property list registers public.json as an alternate document type).
inline constexpr std::string_view CONFIG_TYPE_IDENTIFIER = "com.iainchesworthlabs.fieldtest.config";
inline constexpr std::string_view CONFIG_TYPE_DESCRIPTION = "Field Test setup";
inline constexpr std::string_view CONFIG_EXTENSION = "ejconfig";
inline constexpr std::string_view JSON_EXTENSION = "json";

/// The name of the file the export shares.
inline constexpr std::string_view CONFIG_EXPORT_FILE_NAME = "field-test-setup.ejconfig";

/// The largest setup file the importer reads (the Stage 1 limit).
inline constexpr std::uintmax_t CONFIG_MAX_BYTES = 1024ULL * 1024ULL;

} // namespace ej::bridge
