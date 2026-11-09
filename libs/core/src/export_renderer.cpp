// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"

#include <algorithm>

#include "error_journaling/export_format.hpp"
#include "error_journaling/header_history.hpp"
#include "error_journaling/unit_number.hpp"

namespace ej
{

namespace
{

/// The byte length of the white space character at `i`, or 0 if it is not one. White space is every ASCII control
/// character and the space, plus NEL, the no-break space and the two Unicode line and paragraph separators.
std::size_t white_space_length(std::string_view text, std::size_t i)
{
    const auto byte = [&](std::size_t k) { return k < text.size() ? static_cast<unsigned char>(text[k]) : 0U; };
    const unsigned c = byte(i);
    if (c <= 0x20 || c == 0x7F)
    {
        return 1;
    }
    if (c == 0xC2 && (byte(i + 1) == 0x85 || byte(i + 1) == 0xA0))
    {
        return 2;
    }
    if (c == 0xE2 && byte(i + 1) == 0x80 && (byte(i + 2) == 0xA8 || byte(i + 2) == 0xA9))
    {
        return 3;
    }
    return 0;
}

/// Rule 9: runs of white space, line breaks included, become one space, and the ends are trimmed, so a value is always
/// one line with no trailing white space.
std::string collapse(std::string_view text)
{
    std::string out;
    bool pending_space = false;
    for (std::size_t i = 0; i < text.size();)
    {
        if (const auto length = white_space_length(text, i); length != 0)
        {
            pending_space = !out.empty();
            i += length;
            continue;
        }
        if (pending_space)
        {
            out += ' ';
            pending_space = false;
        }
        out += text[i++];
    }
    return out;
}

std::string format_date(std::chrono::year_month_day date)
{
    const auto month = static_cast<unsigned>(date.month());
    std::string out = std::to_string(static_cast<unsigned>(date.day()));
    out += ' ';
    out += month >= 1 && month <= 12 ? export_format::MONTH_NAMES[month - 1] : std::string_view("???");
    out += ' ';
    out += std::to_string(static_cast<int>(date.year()));
    return out;
}

void append_labelled(std::string& out, std::string_view label, std::string_view value)
{
    out += label;
    out += export_format::LABEL_COLON;
    if (!value.empty())
    {
        out += ' ';
        out += value;
    }
}

Result<std::string, RenderError> location_name(const ExportContext& context, LocationId id)
{
    const auto name = context.m_LocationName ? context.m_LocationName(id) : std::string_view{};
    if (name.empty())
    {
        return fail(RenderError::UNKNOWN_LOCATION);
    }
    return std::string(name);
}

Result<std::string, RenderError> error_type_name(const ExportContext& context, ErrorTypeId id)
{
    const auto name = context.m_ErrorTypeName ? context.m_ErrorTypeName(id) : std::string_view{};
    if (name.empty())
    {
        return fail(RenderError::UNKNOWN_ERROR_TYPE);
    }
    return std::string(name);
}

/// The name an error entry prints: the snapshot taken at the tap when it has one (a later rename does not touch it),
/// otherwise the name looked up by id (data written before snapshots).
Result<std::string, RenderError> entry_type_name(const ExportContext& context, const ErrorEntry& entry)
{
    if (!entry.m_ErrorTypeLabel.empty())
    {
        return entry.m_ErrorTypeLabel;
    }
    return error_type_name(context, entry.m_ErrorType);
}

/// A header value as the export prints it, from its stored text (empty text is an empty value).
Result<std::string, RenderError> display_value(const ExportContext& context, HeaderField field, std::string_view stored)
{
    if (stored.empty())
    {
        return std::string{};
    }
    switch (field)
    {
    case HeaderField::UNIT_NUMBER:
    {
        const auto unit = UnitNumber::parse_stored(stored);
        return unit ? unit->format(context.m_UnitPrefix) : collapse(stored);
    }
    case HeaderField::LOCATION:
    {
        if (const auto id = Uuid::parse(stored))
        {
            return location_name(context, LocationId(*id));
        }
        return collapse(stored);
    }
    case HeaderField::SOFTWARE_VERSION: return collapse(stored);
    }
    return collapse(stored);  // GCOVR_EXCL_LINE unreachable: every HeaderField is handled above
}

std::string field_label(const ExportContext& context, HeaderField field)
{
    switch (field)
    {
    case HeaderField::UNIT_NUMBER: return unit_label(context.m_UnitPrefix);
    case HeaderField::LOCATION: return std::string(export_format::LABEL_LOCATION);
    case HeaderField::SOFTWARE_VERSION: return std::string(export_format::LABEL_SOFTWARE_VERSION);
    }
    return {};  // GCOVR_EXCL_LINE unreachable: every HeaderField is handled above
}

Result<std::string, RenderError> format_entry_time(const ExportContext& context, const Timestamp& when)
{
    if (context.m_Zones == nullptr)
    {
        return fail(RenderError::UNKNOWN_ZONE);
    }
    const auto local = context.m_Zones->to_local(when);
    if (!local)
    {
        return fail(RenderError::UNKNOWN_ZONE);
    }
    return format_time(*local, context.m_TimeFormat);
}

Result<std::string, RenderError> render_line(const Entry& entry, const HeaderHistory& history,
                                             const ExportContext& context)
{
    const auto time = format_entry_time(context, entry.m_Captured);
    if (!time)
    {
        return fail(time.error());
    }
    std::string line = *time;
    line += ' ';

    if (const auto* error = std::get_if<ErrorEntry>(&entry.m_Body))
    {
        const auto name = entry_type_name(context, *error);
        if (!name)
        {
            return fail(name.error());
        }
        line += export_format::ERROR_MARKER;
        line += *name;
        if (error->m_AttachedNote)
        {
            if (const auto note = collapse(*error->m_AttachedNote); !note.empty())
            {
                line += export_format::ATTACHED_NOTE_SEPARATOR;
                line += note;
            }
        }
    }
    else if (const auto* note = std::get_if<NoteEntry>(&entry.m_Body))
    {
        std::string text;
        if (note->m_Template)
        {
            const NoteTemplate* note_template = context.m_Template ? context.m_Template(*note->m_Template) : nullptr;
            if (note_template == nullptr)
            {
                return fail(RenderError::UNKNOWN_TEMPLATE);
            }
            text = collapse(render_note(*note_template, note->m_Value));
        }
        else
        {
            text = collapse(note->m_Value);
        }
        line += export_format::NOTE_MARKER;
        if (!text.empty())
        {
            line += ' ';
            line += text;
        }
    }
    else
    {
        const auto& change = std::get<HeaderChangeEntry>(entry.m_Body);
        const auto old_value =
            display_value(context, change.m_Field, history.previous_value(entry).value_or(std::string{}));
        if (!old_value)
        {
            return fail(old_value.error());
        }
        const auto new_value = display_value(context, change.m_Field, change.m_NewValue);
        if (!new_value)
        {
            return fail(new_value.error());
        }
        line += field_label(context, change.m_Field);
        line += export_format::CHANGED_SUFFIX;
        line += *old_value;
        line += export_format::CHANGE_ARROW;
        line += *new_value;
    }
    return line;
}

Result<std::string, RenderError> render_totals(const Log& log, const ExportContext& context)
{
    std::vector<std::pair<ErrorTypeId, std::size_t>> counts;
    // A type's totals name is the snapshot of its first live entry in time order, so a rename mid-day does not split
    // the count; a type whose entries carry no snapshot is named by lookup.
    std::vector<std::pair<ErrorTypeId, std::string>> snapshots;
    const auto bump = [&counts](ErrorTypeId id)
    {
        const auto it = std::find_if(counts.begin(), counts.end(), [id](const auto& c) { return c.first == id; });
        if (it == counts.end())
        {
            counts.emplace_back(id, 1);
        }
        else
        {
            ++it->second;
        }
    };
    for (const Entry* entry : log.sorted_entries(false))
    {
        if (const auto* error = std::get_if<ErrorEntry>(&entry->m_Body))
        {
            bump(error->m_ErrorType);
            const auto has_snapshot = std::any_of(snapshots.begin(), snapshots.end(),
                                                  [error](const auto& s) { return s.first == error->m_ErrorType; });
            if (!has_snapshot && !error->m_ErrorTypeLabel.empty())
            {
                snapshots.emplace_back(error->m_ErrorType, error->m_ErrorTypeLabel);
            }
        }
    }
    if (counts.empty())
    {
        return std::string{};
    }

    // Declared order first, then anything else in order of first use.
    std::vector<std::pair<ErrorTypeId, std::size_t>> ordered;
    for (const ErrorTypeId declared : context.m_TotalsOrder)
    {
        const auto it = std::find_if(counts.begin(), counts.end(), [declared](const auto& c) { return c.first == declared; });
        if (it != counts.end() && std::none_of(ordered.begin(), ordered.end(),
                                               [declared](const auto& c) { return c.first == declared; }))
        {
            ordered.push_back(*it);
        }
    }
    for (const auto& count : counts)
    {
        if (std::none_of(ordered.begin(), ordered.end(), [&count](const auto& c) { return c.first == count.first; }))
        {
            ordered.push_back(count);
        }
    }

    std::string out(export_format::TOTALS_PREFIX);
    for (std::size_t i = 0; i < ordered.size(); ++i)
    {
        const auto snapshot = std::find_if(snapshots.begin(), snapshots.end(),
                                           [&](const auto& s) { return s.first == ordered[i].first; });
        const auto name = snapshot != snapshots.end() ? Result<std::string, RenderError>(snapshot->second)
                                                      : error_type_name(context, ordered[i].first);
        if (!name)
        {
            return fail(name.error());
        }
        if (i != 0)
        {
            out += export_format::TOTALS_SEPARATOR;
        }
        out += *name;
        out += export_format::TOTALS_COUNT_SEPARATOR;
        out += std::to_string(ordered[i].second);
    }
    return out;
}

}  // namespace

Result<std::string, RenderError> render_header(const Log& log, const ExportContext& context)
{
    const HeaderValues header = HeaderHistory(log).start();

    std::string location;
    if (header.m_Location)
    {
        if (header.m_Location->m_Id)
        {
            const auto name = location_name(context, *header.m_Location->m_Id);
            if (!name)
            {
                return fail(name.error());
            }
            location = *name;
        }
        else
        {
            location = collapse(header.m_Location->m_Text);
        }
    }

    std::string out;
    append_labelled(out, export_format::LABEL_FACILITATOR, collapse(header.m_Facilitator));
    out += export_format::LINE_END;
    append_labelled(out, export_format::LABEL_DATE, format_date(header.m_LocalDate));
    out += export_format::LINE_END;
    append_labelled(out, unit_label(context.m_UnitPrefix),
                    header.m_UnitNumber ? header.m_UnitNumber->format(context.m_UnitPrefix) : std::string{});
    out += export_format::LINE_END;
    append_labelled(out, export_format::LABEL_LOCATION, location);
    out += export_format::LINE_END;
    append_labelled(out, export_format::LABEL_SOFTWARE_VERSION,
                    header.m_SoftwareVersion ? header.m_SoftwareVersion->format() : std::string{});
    return out;
}

Result<std::string, RenderError> render_entry_line(const Log& log, EntryId id, const ExportContext& context)
{
    const Entry* entry = log.find(id);
    if (entry == nullptr)
    {
        return fail(RenderError::UNKNOWN_ENTRY);
    }
    return render_line(*entry, HeaderHistory(log), context);
}

Result<std::string, RenderError> render_export(const Log& log, const ExportContext& context)
{
    auto out = render_header(log, context);
    if (!out)
    {
        return out;
    }

    const HeaderHistory history(log);
    bool first_line = true;
    for (const Entry* entry : log.sorted_entries(false))
    {
        const auto line = render_line(*entry, history, context);
        if (!line)
        {
            return fail(line.error());
        }
        *out += first_line ? "\n\n" : "\n";
        *out += *line;
        first_line = false;
    }

    if (context.m_IncludeTotals)
    {
        const auto totals = render_totals(log, context);
        if (!totals)
        {
            return fail(totals.error());
        }
        if (!totals->empty())
        {
            *out += "\n\n";
            *out += *totals;
        }
    }
    return out;
}

}  // namespace ej
