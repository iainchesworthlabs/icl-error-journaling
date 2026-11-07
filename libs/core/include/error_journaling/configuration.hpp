// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/config_seam.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/note_template.hpp"
#include "error_journaling/result.hpp"

namespace ej
{

inline constexpr int CONFIG_SCHEMA_VERSION = 1;

/// Limits on an imported file, so reading one never does unbounded work.
inline constexpr std::size_t CONFIG_MAX_BYTES = 1024 * 1024;
inline constexpr std::size_t CONFIG_MAX_ENTRIES_PER_LIST = 500;
inline constexpr int CONFIG_BOARD_POSITIONS = 11;

// SEAM (1.7): this alias moves to the real `TimeFormat`.
using ConfigTimeFormat = config_seam::TimeFormat;

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
    ConfigTimeFormat m_TimeFormat;
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

    Code m_Code = Code::MALFORMED_JSON;
    std::string m_Path;     // JSON pointer, e.g. "/error_types/3/name"; empty for the whole file
    std::string m_Message;  // plain English for the person who prepared the file
};

/// Reads a configuration file (UTF-8, a leading byte-order mark is accepted). Every problem found is returned, in file
/// order, not just the first. Files over `CONFIG_MAX_BYTES` or with more than `CONFIG_MAX_ENTRIES_PER_LIST` entries in a
/// list are rejected.
[[nodiscard]] Result<Configuration, std::vector<ConfigError>> import_configuration(std::string_view json);

/// Stable key order, two-space indent, LF, `schema_version` first, one trailing newline.
[[nodiscard]] std::string export_configuration(const Configuration& configuration);

}  // namespace ej
