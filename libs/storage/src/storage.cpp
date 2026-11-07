// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage.hpp"

namespace error_journaling::storage
{

std::string_view backend_name() noexcept
{
    return "sqlite";
}

}  // namespace error_journaling::storage
