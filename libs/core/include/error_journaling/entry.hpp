// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/header_field.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/timestamp.hpp"

#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace ej
{

struct ErrorEntry
{
    ErrorTypeId m_ErrorType;
    std::optional<std::string> m_AttachedNote;
    /// The name of the error type as it read when the entry was written (Stage 2, decision 5), so the entry still reads
    /// the same after the type is renamed or removed from the configuration. Storage requires it for a new entry.
    std::string m_ErrorTypeLabel = {};

    enum class Error
    {
        EMPTY_LABEL
    };

    /// An entry made from a tap: the snapshot is required, so the name at the moment of the tap is never missing.
    /// Entries read from older data may have an empty label; the export renderer then looks the name up by id.
    [[nodiscard]] static Result<ErrorEntry, Error> from_tap(ErrorTypeId type, std::string label,
                                                            std::optional<std::string> attached_note = std::nullopt)
    {
        if (label.empty())
        {
            return fail(Error::EMPTY_LABEL);
        }
        return ErrorEntry{type, std::move(attached_note), std::move(label)};
    }

    /// Changing the entry's type takes a new snapshot; the attached note stays.
    [[nodiscard]] Result<void, Error> retype(ErrorTypeId type, std::string label)
    {
        if (label.empty())
        {
            return fail(Error::EMPTY_LABEL);
        }
        m_ErrorType = type;
        m_ErrorTypeLabel = std::move(label);
        return {};
    }
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

/// What the tester did on the optional second board (docs/plan/feedback-interactions-board.md, 3.1). Like an error
/// entry it holds the type's id and a snapshot of its name. It holds people by id only, never a name.
struct InteractionEntry
{
    InteractionTypeId m_Type;
    /// The name of the interaction type as it read when the entry was written.
    std::string m_TypeLabel;
    /// The people involved, in the order they were picked, without repeats; at most MAX_PEOPLE_PER_INTERACTION.
    std::vector<PersonId> m_People;
    /// The answer to the type's {value} slot.
    std::optional<std::string> m_Value;
    std::optional<std::string> m_AttachedNote;
};

using EntryBody = std::variant<ErrorEntry, NoteEntry, HeaderChangeEntry, InteractionEntry>;

struct Entry
{
    EntryId m_Id;
    Timestamp m_Captured;
    std::optional<UtcMillis> m_EditedUtc;
    bool m_Deleted = false;
    EntryBody m_Body;
};

}  // namespace ej
