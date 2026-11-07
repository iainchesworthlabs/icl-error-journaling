// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace error_journaling::storage
{

/// The kind of storage in use. "none" until Stage 2 puts the write-ahead log here.
[[nodiscard]] std::string_view backend_name() noexcept;

}  // namespace error_journaling::storage
