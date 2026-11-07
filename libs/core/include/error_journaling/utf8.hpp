// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

namespace ej
{

/// The number of Unicode code points in `text`, or nothing when it is not well-formed UTF-8 (overlong forms,
/// surrogates and values above U+10FFFF are malformed). Storage limits are counted in code points.
[[nodiscard]] std::optional<std::size_t> utf8_code_points(std::string_view text);

} // namespace ej
