// SPDX-License-Identifier: GPL-3.0-or-later
#include <windows.h>

#include <array>
#include <string>

#include "error_journaling/platform.hpp"

namespace error_journaling::platform
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

}  // namespace error_journaling::platform
