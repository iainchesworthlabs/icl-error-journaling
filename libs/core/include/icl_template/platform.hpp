// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>

namespace icl_template::platform
{

/// The name of the machine, or "unknown" when the system does not provide one.
/// One implementation per platform lives in src/platform/<os>/host_name.cpp; CMake adds
/// the directory for the target, and no source file tests for the platform.
[[nodiscard]] std::string host_name();

}  // namespace icl_template::platform
