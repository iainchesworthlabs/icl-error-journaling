// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace ej::app
{

/// Which commit, build and date the program was made from, for the About page. The values are written by
/// tools/licences/build_info.py on every build (cmake/EjAbout.cmake) into a source file of the build directory, so a
/// program built outside git, or without it, says "unknown" and does not fail. `m_BuildDate` is the date of the commit,
/// or $SOURCE_DATE_EPOCH when the build is given one, and never the time of the build: the same commit gives the same
/// program.
struct BuildInfo
{
    std::string_view m_Commit;      ///< The short hash, or "unknown".
    bool m_Dirty = false;           ///< A tracked file differed from the commit when the program was built.
    std::string_view m_Ref;         ///< The tag HEAD was at, else the branch; empty when neither is known.
    std::string_view m_BuildNumber; ///< The iOS CFBundleVersion, else the CI run number, else "unknown".
    std::string_view m_BuildDate;   ///< "2026-11-09 03:21 UTC" or "unknown".
    std::string_view m_SourceUrl;   ///< The project's homepage, which is where the source is.
};

/// The values of this build: defined in the generated file about/build_info_values.cpp of the build directory.
[[nodiscard]] BuildInfo generated_build_info();

} // namespace ej::app
