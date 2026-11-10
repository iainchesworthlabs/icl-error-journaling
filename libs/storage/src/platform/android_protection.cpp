// SPDX-License-Identifier: GPL-3.0-or-later
#include "android_protection.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace ej::platform
{

namespace
{

bool all_digits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; });
}

// The next part of `rest` up to a slash, which is cut off `rest` together with the slash.
std::string_view take(std::string_view& rest)
{
    const auto slash = rest.find('/');
    const std::string_view part = rest.substr(0, slash);
    rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
    return part;
}

// True when `rest` has a package name and then something below it, as in "com.example.app/files/ej.db".
bool inside_package(std::string_view rest)
{
    const std::string_view package = take(rest);
    return !package.empty() && !rest.empty();
}

} // namespace

ProtectionClass android_protection_class(const std::filesystem::path& path)
{
    // No is_absolute(): "/data/..." has no drive letter, so it is not absolute where the tests also run (Windows). A
    // relative path starts with none of the prefixes below, so it is OTHER anyway.
    const std::string text = path.lexically_normal().generic_string();
    // Normalising resolves a ".." it can; one that is left is part of a name ("a..b"), and the answer for an odd spelling
    // is the cautious one.
    if (text.find("..") != std::string::npos)
    {
        return ProtectionClass::OTHER;
    }

    std::string_view rest = text;
    if (rest.rfind("/data/user/", 0) == 0)
    {
        rest.remove_prefix(std::string_view("/data/user/").size());
        return all_digits(take(rest)) && inside_package(rest) ? ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION
                                                              : ProtectionClass::OTHER;
    }
    if (rest.rfind("/data/data/", 0) == 0)
    {
        rest.remove_prefix(std::string_view("/data/data/").size());
        return inside_package(rest) ? ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION : ProtectionClass::OTHER;
    }
    if (rest.rfind("/data/user_de/", 0) == 0)
    {
        rest.remove_prefix(std::string_view("/data/user_de/").size());
        return all_digits(take(rest)) && inside_package(rest) ? ProtectionClass::NONE : ProtectionClass::OTHER;
    }
    return ProtectionClass::OTHER;
}

} // namespace ej::platform
