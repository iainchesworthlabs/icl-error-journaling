// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/export_renderer.hpp"

#include "error_journaling/export_format.hpp"
#include "error_journaling/unit_number.hpp"

namespace ej
{

namespace
{

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

void append_line(std::string& out, std::string_view label, std::string_view value)
{
    out += label;
    out += export_format::LABEL_COLON;
    if (!value.empty())
    {
        out += ' ';
        out += value;
    }
}

}  // namespace

Result<std::string, RenderError> render_header(const Log& log, const ExportContext& context)
{
    const HeaderValues& header = log.initial_header();

    std::string location;
    if (header.m_Location)
    {
        if (header.m_Location->m_Id)
        {
            const auto name = context.m_LocationName ? context.m_LocationName(*header.m_Location->m_Id)
                                                     : std::string_view{};
            if (name.empty())
            {
                return fail(RenderError::UNKNOWN_LOCATION);
            }
            location = name;
        }
        else
        {
            location = header.m_Location->m_Text;
        }
    }

    std::string out;
    append_line(out, export_format::LABEL_FACILITATOR, header.m_Facilitator);
    out += export_format::LINE_END;
    append_line(out, export_format::LABEL_DATE, format_date(header.m_LocalDate));
    out += export_format::LINE_END;
    append_line(out, unit_label(context.m_UnitPrefix),
                header.m_UnitNumber ? header.m_UnitNumber->format(context.m_UnitPrefix) : std::string{});
    out += export_format::LINE_END;
    append_line(out, export_format::LABEL_LOCATION, location);
    out += export_format::LINE_END;
    append_line(out, export_format::LABEL_SOFTWARE_VERSION,
                header.m_SoftwareVersion ? header.m_SoftwareVersion->format() : std::string{});
    return out;
}

Result<std::string, RenderError> render_export(const Log& log, const ExportContext& context)
{
    // Skeleton (work package 1.2): the header block only. The timeline arrives in 1.5.
    return render_header(log, context);
}

}  // namespace ej
