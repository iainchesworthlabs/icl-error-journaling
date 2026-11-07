// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace error_journaling::storage
{

/// The kind of storage the application uses: "sqlite" (the store `app::start_sqlite_storage` opens, Stage 3).
[[nodiscard]] std::string_view backend_name() noexcept;

}  // namespace error_journaling::storage
