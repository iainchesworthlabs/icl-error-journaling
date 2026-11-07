// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <expected>
#include <utility>

namespace ej
{

/// The error convention of the core: a recoverable failure is a `std::expected` whose error is a small enum owned by
/// the component (`UnitNumber::Error`, `Log::Error`, ...). Nothing throws across the library boundary.
template <typename T, typename E>
using Result = std::expected<T, E>;

/// `return fail(Error::EMPTY);` instead of spelling out `std::unexpected`.
template <typename E>
[[nodiscard]] constexpr std::unexpected<std::decay_t<E>> fail(E&& error)
{
    return std::unexpected<std::decay_t<E>>(std::forward<E>(error));
}

}  // namespace ej
