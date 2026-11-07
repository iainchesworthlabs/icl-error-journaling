// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>

#include "icl_template/platform.hpp"

namespace icl_template::platform
{

// A browser has no host name to report.
std::string host_name()
{
    return "browser";
}

}  // namespace icl_template::platform
