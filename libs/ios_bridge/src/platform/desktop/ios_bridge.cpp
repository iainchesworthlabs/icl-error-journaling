// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/ios_bridge.hpp"

namespace error_journaling::bridge
{

std::string_view implementation_name() noexcept
{
    return "desktop-stub";
}

}  // namespace error_journaling::bridge
