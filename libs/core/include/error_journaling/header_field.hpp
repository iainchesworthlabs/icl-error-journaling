// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string_view>

namespace ej
{

/// The header fields that can change during the day (HDR-05). Facilitator and date are fixed for a log.
enum class HeaderField
{
    UNIT_NUMBER,
    LOCATION,
    SOFTWARE_VERSION,
    EXTRA_VERSIONS
};

/// The key used in stored data and configuration: "unit_number", "location", "software_version", "extra_versions".
[[nodiscard]] constexpr std::string_view header_field_key(HeaderField field)
{
    switch (field)
    {
    case HeaderField::UNIT_NUMBER: return "unit_number";
    case HeaderField::LOCATION: return "location";
    case HeaderField::SOFTWARE_VERSION: return "software_version";
    case HeaderField::EXTRA_VERSIONS: return "extra_versions";
    }
    return {};
}

[[nodiscard]] constexpr std::optional<HeaderField> header_field_from_key(std::string_view key)
{
    for (const auto field : {HeaderField::UNIT_NUMBER, HeaderField::LOCATION, HeaderField::SOFTWARE_VERSION, HeaderField::EXTRA_VERSIONS})
    {
        if (header_field_key(field) == key)
        {
            return field;
        }
    }
    return std::nullopt;
}

}  // namespace ej
