// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <array>
#include <string>

#include "icl_template/platform.hpp"

namespace icl_template::platform
{

std::string host_name()
{
    std::array<char, MAX_COMPUTERNAME_LENGTH + 1> buffer{};
    DWORD size = static_cast<DWORD>(buffer.size());
    if (::GetComputerNameA(buffer.data(), &size) == 0 || size == 0)
    {
        return "unknown";
    }
    return std::string(buffer.data(), size);
}

}  // namespace icl_template::platform
