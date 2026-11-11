// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/export_format.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"
#include "error_journaling/note_template.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/time_format.hpp"
#include "error_journaling/time_zone_provider.hpp"

namespace ej
{

/// Everything the renderer needs besides the log. The name lookups return an empty view (or a null pointer) for an id
/// they do not know; retired error types must still resolve.
struct ExportContext
{
    std::string_view m_UnitPrefix;
    /// The text before the error name on an error line (PRF-13). May be empty, and then there is no space for it either.
    std::string_view m_ErrorPrefix = export_format::DEFAULT_ERROR_PREFIX;
    TimeFormat m_TimeFormat;
    bool m_IncludeTotals = false;
    const TimeZoneProvider* m_Zones = nullptr;
    std::function<std::string_view(ErrorTypeId)> m_ErrorTypeName;
    std::function<std::string_view(LocationId)> m_LocationName;
    std::function<const NoteTemplate*(NoteTemplateId)> m_Template;
    /// The order of the totals line: board order, then master-list order (PRF-10). An error type used in the log but
    /// missing here is placed after these, in order of first use.
    std::vector<ErrorTypeId> m_TotalsOrder;
};

enum class RenderError
{
    UNKNOWN_ERROR_TYPE,
    UNKNOWN_LOCATION,
    UNKNOWN_TEMPLATE,
    UNKNOWN_ZONE,
    UNKNOWN_ENTRY
};

/// The header block from the log's initial values: five lines, no trailing newline.
[[nodiscard]] Result<std::string, RenderError> render_header(const Log& log, const ExportContext& context);

/// The whole log: header block, a blank line, the timeline of live entries in time order and, when totals are on and
/// there is at least one error, a blank line and the totals line. LF line endings, no trailing newline. On an error
/// nothing is returned, so no partial text can leak.
[[nodiscard]] Result<std::string, RenderError> render_export(const Log& log, const ExportContext& context);

/// One entry's line exactly as `render_export` prints it (REV-04, and the timeline in Stage 3). A deleted entry is
/// rendered too.
[[nodiscard]] Result<std::string, RenderError> render_entry_line(const Log& log, EntryId id,
                                                                 const ExportContext& context);

}  // namespace ej
