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

// Timeline lines. <time> is formatted by format_time.

/// The text between the time and the error name on an error line when the facilitator has not chosen another (PRF-13;
/// accepted: "10:05 T| Wrong Response"). The space after the prefix is part of the line format and is written only when
/// the prefix is not empty.
inline constexpr std::string_view DEFAULT_ERROR_PREFIX = "T|";
/// The longest the prefix may be, in characters.
inline constexpr int MAX_ERROR_PREFIX_CHARACTERS = 12;
/// Between an error name and its attached note (an en dash with a space each side).
inline constexpr std::string_view ATTACHED_NOTE_SEPARATOR = " \xE2\x80\x93 ";
/// After the time on a note line.
inline constexpr std::string_view NOTE_MARKER = "(Free Text)";
/// After the field label on a header-change line, and between the old and the new value (an arrow).
inline constexpr std::string_view CHANGED_SUFFIX = " changed: ";
inline constexpr std::string_view CHANGE_ARROW = " \xE2\x86\x92 ";

// Totals line (PRF-10). PROVISIONAL: the PRD does not give the wording (plan, open question 1), so this is the plan's
// proposal: "Totals: Wrong Response 3, Mute Delay 1". Changing it means editing these three constants and regenerating
// totals-on.txt, totals-on-empty.txt and deleted-entries.txt.
inline constexpr std::string_view TOTALS_PREFIX = "Totals: ";
inline constexpr std::string_view TOTALS_SEPARATOR = ", ";
inline constexpr std::string_view TOTALS_COUNT_SEPARATOR = " ";

}  // namespace ej::export_format
