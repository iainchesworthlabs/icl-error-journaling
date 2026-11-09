// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

#include "error_journaling/header_field.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/software_version.hpp"
#include "error_journaling/unit_number.hpp"

namespace ej
{

/// A location from the preference list (an id) or free text from Other.
struct LocationValue
{
    std::optional<LocationId> m_Id;
    std::string m_Text;
};

/// The header of a log at one moment. Each field after the date may be empty.
struct HeaderValues
{
    std::string m_Facilitator;
    std::chrono::year_month_day m_LocalDate;
    std::optional<UnitNumber> m_UnitNumber;
    std::optional<LocationValue> m_Location;
    std::optional<SoftwareVersion> m_SoftwareVersion;
};

/// Why a header edit was refused. For an invalid value, the component's own error is carried along.
struct HeaderError
{
    enum class Kind
    {
        INVALID_UNIT_NUMBER,      // m_UnitNumber says why
        INVALID_SOFTWARE_VERSION, // m_SoftwareVersion says why
        EMPTY_LOCATION,
        UNCHANGED,  // the new value equals the one in force (or, for a correction, the one before the change)
        UNKNOWN_ENTRY,
        NOT_A_HEADER_CHANGE,
        DUPLICATE_ID
    };

    Kind m_Kind = Kind::UNCHANGED;
    std::optional<UnitNumber::Error> m_UnitNumber;
    std::optional<SoftwareVersion::Error> m_SoftwareVersion;

    friend bool operator==(const HeaderError&, const HeaderError&) = default;
};

/// The stored text of a field: the unit number's digits and, when it has an environment, a dash and the environment
/// ("27" or "27-prod"), the software version as major.minor.patch, and for a location the id formatted as a UUID or
/// else the free text. Empty optional when the field is empty.
[[nodiscard]] std::optional<std::string> header_field_text(const HeaderValues& header, HeaderField field);

/// Sets a field from its stored text, validating it. A location is an id when the text is a UUID, otherwise free text.
/// Empty text is an error (EMPTY from the component, EMPTY_LOCATION for a location); use `clear_header_field` to empty.
[[nodiscard]] Result<void, HeaderError> set_header_field(HeaderValues& header, HeaderField field, std::string_view text);

void clear_header_field(HeaderValues& header, HeaderField field);

}  // namespace ej
