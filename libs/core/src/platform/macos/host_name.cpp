// SPDX-License-Identifier: GPL-3.0-or-later
#include <unistd.h>

#include <array>
#include <string>

#include "icl_template/platform.hpp"

namespace icl_template::platform
{

std::string host_name()
{
    std::array<char, 256> buffer{};
    // The failure branch needs gethostname to fail, which a test cannot cause.
    if (::gethostname(buffer.data(), buffer.size() - 1) != 0 || buffer[0] == '\0')  // GCOVR_EXCL_BR_LINE
    {
        return "unknown";
    }
    return buffer.data();
}

}  // namespace icl_template::platform
