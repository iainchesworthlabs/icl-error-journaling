// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace error_journaling::bridge
{

/// Which implementation of the bridge is linked: "ios" on a phone, "desktop-stub" everywhere else. One implementation
/// per directory under src/platform/; CMake adds the one that matches the target and no source file tests for the
/// platform.
[[nodiscard]] std::string_view implementation_name() noexcept;

}  // namespace error_journaling::bridge
