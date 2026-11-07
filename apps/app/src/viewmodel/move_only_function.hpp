// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>

namespace ej::viewmodel
{

/// The callable type of the view-model layer: a function that can own a move-only capture (a `std::unique_ptr`, a
/// result slot). It is one alias so that, if a standard library of the build matrix lacks `std::move_only_function`,
/// the replacement is made in this file and nowhere else.
template <typename Signature>
using MoveOnlyFunction = std::move_only_function<Signature>;

} // namespace ej::viewmodel
