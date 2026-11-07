// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace error_journaling::storage
{

/// The kind of storage the application uses. "none" until Stage 3 opens a store in the application; the repositories exist.
[[nodiscard]] std::string_view backend_name() noexcept;

}  // namespace error_journaling::storage
