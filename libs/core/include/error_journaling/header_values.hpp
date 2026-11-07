// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>
#include <optional>
#include <string>

#include "error_journaling/ids.hpp"
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

}  // namespace ej
