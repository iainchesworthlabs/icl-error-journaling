// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace ej::export_format
{

// The export is a contract with the downstream ticket indexer (PRD, Export format). Each constant is defined once here.

inline constexpr std::string_view LABEL_FACILITATOR = "Facilitator";
inline constexpr std::string_view LABEL_DATE = "Date";
inline constexpr std::string_view LABEL_LOCATION = "Location";
inline constexpr std::string_view LABEL_SOFTWARE_VERSION = "Software Version";

/// Between a label and its value. An empty value prints the label and this colon only.
inline constexpr std::string_view LABEL_COLON = ":";

/// Month names of the header date ("7 Nov 2026"); fixed English so the export never follows the phone's language.
inline constexpr std::string_view MONTH_NAMES[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

inline constexpr char LINE_END = '\n';

}  // namespace ej::export_format
