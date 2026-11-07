// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>

#include "error_journaling/platform.hpp"

namespace error_journaling::platform
{

// A browser has no host name to report.
std::string host_name()
{
    return "browser";
}

}  // namespace error_journaling::platform
