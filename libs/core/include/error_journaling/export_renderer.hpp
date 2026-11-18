// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/export_format.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/interaction.hpp"
#include "error_journaling/person.hpp"
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
    /// The label after the time on an interaction line (preference `interaction_marker`). Empty drops it, with its
    /// space.
    std::string_view m_InteractionMarker = export_format::DEFAULT_INTERACTION_MARKER;
    /// How a person's name is written (`name_export`). Text that leaves the device uses the preference; the Review
    /// screen uses FULL. Names appear only inside an interaction's line, never as a list.
    NameLevel m_NameLevel = NameLevel::INITIALS;
    /// The name of an interaction type by id, for an entry without a snapshot. Empty for an unknown id.
    std::function<std::string_view(InteractionTypeId)> m_InteractionTypeName;
    /// The current line pattern of an interaction type, if it has one.
    std::function<std::optional<std::string>(InteractionTypeId)> m_InteractionPattern;
    /// The name of a person, or none for one who is forgotten or unknown (reads as "someone").
    std::function<std::optional<std::string>(const PersonId&)> m_PersonName;
};

enum class RenderError
{
    UNKNOWN_ERROR_TYPE,
    UNKNOWN_LOCATION,
    UNKNOWN_TEMPLATE,
    UNKNOWN_ZONE,
    UNKNOWN_ENTRY,
    /// An interaction entry with no name snapshot whose type the context does not know.
    UNKNOWN_INTERACTION_TYPE
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


/// The export line of an interaction: `<time> <marker> <text>`, with ` – <note>` after it when a note is attached
/// (feedback page, 3.6). No ticket marker. `text` is the result of `render_interaction_text` at the name level chosen
/// for shared text; it and the note are collapsed to one line. An empty marker is left out, with its space.
[[nodiscard]] std::string render_interaction_export_line(std::string_view time, std::string_view marker,
                                                         std::string_view text,
                                                         const std::optional<std::string>& attached_note);

}  // namespace ej
