// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <string_view>

/// The people of the storage tests: a fixed list of fictional stand-ins (NFR-10), the only names a test may use. Tests
/// that look for a name in files use the list to look for every one of them.
namespace ej::test
{

inline constexpr std::string_view ALEX = "Alex Sample";
inline constexpr std::string_view JORDAN = "Jordan Placeholder";
inline constexpr std::string_view PAT = "Pat Stand-in";
inline constexpr std::string_view SAM = "Sam Example";

inline constexpr std::array<std::string_view, 4> ALL_NAMES{ALEX, JORDAN, PAT, SAM};

} // namespace ej::test
