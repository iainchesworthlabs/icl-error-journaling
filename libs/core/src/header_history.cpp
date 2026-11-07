// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/header_history.hpp"

namespace ej
{

namespace
{

HeaderError error_of(HeaderError::Kind kind)
{
    HeaderError error;
    error.m_Kind = kind;
    return error;
}

/// The canonical stored text of a value for a field, or why it is not valid.
Result<std::string, HeaderError> canonical_text(HeaderField field, std::string_view text)
{
    HeaderValues scratch;
    if (auto set = set_header_field(scratch, field, text); !set)
    {
        return fail(set.error());
    }
    return header_field_text(scratch, field).value_or(std::string{});
}

}  // namespace

HeaderHistory::HeaderHistory(const Log& log) : m_Initial(log.initial_header())
{
    for (const Entry* entry : log.sorted_entries(true))
    {
        if (const auto* change = std::get_if<HeaderChangeEntry>(&entry->m_Body))
        {
            m_Changes.push_back(
                Change{entry->m_Captured.m_Utc, change->m_Field, change->m_NewValue, entry->m_Id, entry->m_Deleted});
        }
    }
}

HeaderValues HeaderHistory::at(UtcMillis when) const
{
    HeaderValues header = m_Initial;
    for (const Change& change : m_Changes)
    {
        if (change.m_Deleted || change.m_Utc > when)
        {
            continue;
        }
        (void)set_header_field(header, change.m_Field, change.m_Text);  // stored values were validated on the way in
    }
    return header;
}

std::optional<std::string> HeaderHistory::previous_value(const Entry& change) const
{
    const auto* body = std::get_if<HeaderChangeEntry>(&change.m_Body);
    if (body == nullptr)
    {
        return std::nullopt;
    }
    HeaderValues header = m_Initial;
    for (const Change& earlier : m_Changes)
    {
        if (earlier.m_Id == change.m_Id)
        {
            return header_field_text(header, body->m_Field);
        }
        if (!earlier.m_Deleted && earlier.m_Field == body->m_Field)
        {
            (void)set_header_field(header, earlier.m_Field, earlier.m_Text);
        }
    }
    return std::nullopt;
}

Result<void, HeaderError> change_header(Log& log, HeaderField field, std::string new_value, Timestamp when, EntryId id)
{
    const auto text = canonical_text(field, new_value);
    if (!text)
    {
        return fail(text.error());
    }
    const HeaderValues in_force = HeaderHistory(log).at(when.m_Utc);
    if (header_field_text(in_force, field) == *text)
    {
        return fail(error_of(HeaderError::Kind::UNCHANGED));
    }
    Entry entry;
    entry.m_Id = id;
    entry.m_Captured = std::move(when);
    entry.m_Body = HeaderChangeEntry{field, *text};
    if (!log.add(std::move(entry)))
    {
        return fail(error_of(HeaderError::Kind::DUPLICATE_ID));
    }
    return {};
}

Result<void, HeaderError> correct_header(Log& log, HeaderField field, std::string value)
{
    const Entry* latest = nullptr;
    for (const Entry* entry : log.sorted_entries(false))
    {
        const auto* change = std::get_if<HeaderChangeEntry>(&entry->m_Body);
        if (change != nullptr && change->m_Field == field)
        {
            latest = entry;
        }
    }
    if (latest != nullptr)
    {
        return correct_header_change(log, latest->m_Id, std::move(value));
    }

    HeaderValues header = log.initial_header();
    if (value.empty())
    {
        clear_header_field(header, field);
    }
    else if (auto set = set_header_field(header, field, value); !set)
    {
        return fail(set.error());
    }
    log.set_initial_header(std::move(header));
    return {};
}

Result<void, HeaderError> correct_header_change(Log& log, EntryId change, std::string new_value)
{
    const Entry* entry = log.find(change);
    if (entry == nullptr)
    {
        return fail(error_of(HeaderError::Kind::UNKNOWN_ENTRY));
    }
    const auto* body = std::get_if<HeaderChangeEntry>(&entry->m_Body);
    if (body == nullptr)
    {
        return fail(error_of(HeaderError::Kind::NOT_A_HEADER_CHANGE));
    }
    const HeaderField field = body->m_Field;
    const auto text = canonical_text(field, new_value);
    if (!text)
    {
        return fail(text.error());
    }
    if (HeaderHistory(log).previous_value(*entry) == *text)
    {
        return fail(error_of(HeaderError::Kind::UNCHANGED));
    }
    (void)log.replace_body(change, HeaderChangeEntry{field, *text});
    return {};
}

}  // namespace ej
