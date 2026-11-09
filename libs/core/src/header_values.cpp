// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/header_values.hpp"

namespace ej
{

namespace
{

HeaderError unit_error(UnitNumber::Error error)
{
    HeaderError result;
    result.m_Kind = HeaderError::Kind::INVALID_UNIT_NUMBER;
    result.m_UnitNumber = error;
    return result;
}

HeaderError version_error(SoftwareVersion::Error error)
{
    HeaderError result;
    result.m_Kind = HeaderError::Kind::INVALID_SOFTWARE_VERSION;
    result.m_SoftwareVersion = error;
    return result;
}

}  // namespace

std::optional<std::string> header_field_text(const HeaderValues& header, HeaderField field)
{
    switch (field)
    {
    case HeaderField::UNIT_NUMBER:
        if (header.m_UnitNumber)
        {
            return header.m_UnitNumber->stored();
        }
        break;
    case HeaderField::LOCATION:
        if (header.m_Location)
        {
            if (header.m_Location->m_Id)
            {
                return header.m_Location->m_Id->format();
            }
            if (!header.m_Location->m_Text.empty())
            {
                return header.m_Location->m_Text;
            }
        }
        break;
    case HeaderField::SOFTWARE_VERSION:
        if (header.m_SoftwareVersion)
        {
            return header.m_SoftwareVersion->stored();
        }
        break;
    }
    return std::nullopt;
}

Result<void, HeaderError> set_header_field(HeaderValues& header, HeaderField field, std::string_view text)
{
    switch (field)
    {
    case HeaderField::UNIT_NUMBER:
    {
        auto parsed = UnitNumber::parse_stored(text);
        if (!parsed)
        {
            return fail(unit_error(parsed.error()));
        }
        header.m_UnitNumber = std::move(*parsed);
        return {};
    }
    case HeaderField::SOFTWARE_VERSION:
    {
        auto parsed = SoftwareVersion::parse_stored(text);
        if (!parsed)
        {
            return fail(version_error(parsed.error()));
        }
        header.m_SoftwareVersion = *parsed;
        return {};
    }
    case HeaderField::LOCATION:
    {
        if (text.empty())
        {
            HeaderError error;
            error.m_Kind = HeaderError::Kind::EMPTY_LOCATION;
            return fail(error);
        }
        if (const auto id = Uuid::parse(text))
        {
            header.m_Location = LocationValue{LocationId(*id), {}};
        }
        else
        {
            header.m_Location = LocationValue{std::nullopt, std::string(text)};
        }
        return {};
    }
    }
    return {};  // GCOVR_EXCL_LINE unreachable: every HeaderField is handled above
}

void clear_header_field(HeaderValues& header, HeaderField field)
{
    switch (field)
    {
    case HeaderField::UNIT_NUMBER: header.m_UnitNumber.reset(); break;
    case HeaderField::LOCATION: header.m_Location.reset(); break;
    case HeaderField::SOFTWARE_VERSION: header.m_SoftwareVersion.reset(); break;
    }
}

}  // namespace ej
