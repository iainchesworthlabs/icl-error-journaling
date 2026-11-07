// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/header_field.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/timestamp.hpp"

#include <optional>
#include <string>
#include <variant>

namespace ej
{

struct ErrorEntry
{
    ErrorTypeId m_ErrorType;
    std::optional<std::string> m_AttachedNote;
    /// The name of the error type as it read when the entry was written (Stage 2, decision 5), so the entry still reads
    /// the same after the type is renamed or removed from the configuration. Storage requires it for a new entry.
    std::string m_ErrorTypeLabel = {};
};

struct NoteEntry
{
    std::optional<NoteTemplateId> m_Template;
    std::string m_Value;  // the value text when a template is used, the whole text when free
};

struct HeaderChangeEntry
{
    HeaderField m_Field = HeaderField::UNIT_NUMBER;
    std::string m_NewValue;
};

using EntryBody = std::variant<ErrorEntry, NoteEntry, HeaderChangeEntry>;

struct Entry
{
    EntryId m_Id;
    Timestamp m_Captured;
    std::optional<UtcMillis> m_EditedUtc;
    bool m_Deleted = false;
    EntryBody m_Body;
};

}  // namespace ej
