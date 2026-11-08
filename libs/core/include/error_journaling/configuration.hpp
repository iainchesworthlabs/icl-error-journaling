// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/ids.hpp"
#include "error_journaling/note_template.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/time_format.hpp"

namespace ej
{

inline constexpr int CONFIG_SCHEMA_VERSION = 1;

/// Limits on an imported file, so reading one never does unbounded work.
inline constexpr std::size_t CONFIG_MAX_BYTES = 1024 * 1024;
inline constexpr std::size_t CONFIG_MAX_ENTRIES_PER_LIST = 500;
inline constexpr int CONFIG_BOARD_POSITIONS = 11;

struct ErrorType
{
    ErrorTypeId m_Id;
    std::string m_Name;
    bool m_Retired = false;
    /// 1 to 11 on the capture board, or none. A retired type keeps its value but is left out of the layout.
    std::optional<int> m_BoardPosition;

    friend bool operator==(const ErrorType&, const ErrorType&) = default;
};

struct Location
{
    LocationId m_Id;
    std::string m_Name;
    int m_SortOrder = 0;
    bool m_Retired = false;

    friend bool operator==(const Location&, const Location&) = default;
};

struct Configuration
{
    std::string m_Facilitator;
    std::string m_UnitPrefix;
    TimeFormat m_TimeFormat;
    bool m_IncludeTotals = false;
    std::vector<ErrorType> m_ErrorTypes;
    std::vector<Location> m_Locations;
    std::vector<NoteTemplate> m_Templates;
};

struct ConfigError
{
    enum class Code
    {
        MALFORMED_JSON,
        WRONG_TYPE,
        MISSING_FIELD,
        UNKNOWN_FIELD,
        UNSUPPORTED_VERSION,
        DUPLICATE,
        OUT_OF_RANGE,
        BAD_PATTERN,
        BAD_ID
    };

    /// Which problem it is, finer than `m_Code`: what a screen needs to put it in a sentence of its own.
    enum class Problem
    {
        OTHER,
        EMPTY,                // a required value is missing or blank
        TOO_LONG,             // longer than `m_Limit` characters
        NAME_TAKEN,           // `m_Value` is also the name of item `m_Other`
        POSITION_TAKEN,       // board position `m_Value` is also used by item `m_Other`
        NO_PLACEHOLDER,       // a pattern without {value}
        STRAY_BRACE,          // a pattern with a brace that is not closed or doubled
        UNKNOWN_PLACEHOLDER   // a pattern with a placeholder other than {value}
    };

    Code m_Code = Code::MALFORMED_JSON;
    std::string m_Path;     // JSON pointer, e.g. "/error_types/3/name"; empty for the whole file
    std::string m_Message;  // plain English for the person who prepared the file

    // The same problem as data, so that the app can say it in its own words. All are worked out from the path except
    // `m_Problem`, `m_Other`, `m_Value` and `m_Limit`.
    Problem m_Problem = Problem::OTHER;
    std::string m_Section;    // "preferences", "error_types", "locations", "note_templates" or "" (the whole file)
    std::size_t m_Item = 0;   // the 1-based place of the item in its list, or 0
    std::string m_Field;      // the key concerned, e.g. "name" or "time_format/style"
    std::size_t m_Other = 0;  // the 1-based place of the other item, for NAME_TAKEN and POSITION_TAKEN
    std::string m_Value;      // the name or the position that is taken
    int m_Limit = 0;          // the most characters, for TOO_LONG
};

/// Reads a configuration file (UTF-8, a leading byte-order mark is accepted). Every problem found is returned, in file
/// order, not just the first. Files over `CONFIG_MAX_BYTES` or with more than `CONFIG_MAX_ENTRIES_PER_LIST` entries in a
/// list are rejected.
[[nodiscard]] Result<Configuration, std::vector<ConfigError>> import_configuration(std::string_view json);

/// Stable key order, two-space indent, LF, `schema_version` first, one trailing newline.
[[nodiscard]] std::string export_configuration(const Configuration& configuration);

}  // namespace ej
