// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <optional>
#include <string>
#include <vector>

#include "error_journaling/log.hpp"

namespace ej
{

/// The header in force at any time, worked out from the log's initial values and its live header-change entries
/// (HDR-05, HDR-07, DAY-03). A snapshot taken when it is constructed: build a new one after the log changes.
///
/// Correct fixes a typo and writes no entry. Change records a new value at a point in time (an entry). The "old" side of
/// a change line is derived from this history, not stored, so a correction flows into every later line.
class HeaderHistory
{
public:
    explicit HeaderHistory(const Log& log);

    /// The header in force at that instant. A change applies from its own millisecond.
    [[nodiscard]] HeaderValues at(UtcMillis when) const;

    /// What the export header block shows: the values the log started with.
    [[nodiscard]] HeaderValues start() const { return m_Initial; }

    /// The value just before this change, in the stored text of `header_field_text`; empty when the field was empty.
    /// Also empty when the entry is not a header change of this log.
    [[nodiscard]] std::optional<std::string> previous_value(const Entry& change) const;

private:
    struct Change
    {
        UtcMillis m_Utc;
        HeaderField m_Field;
        std::string m_Text;
        EntryId m_Id;
        bool m_Deleted;
    };

    HeaderValues m_Initial;
    std::vector<Change> m_Changes;  // time order, ties by insertion; deleted ones included and flagged
};

/// Records that a field changed at `when`. The value is validated by the field's own type and must differ from the value
/// in force at that time. The entry is stored with the canonical text of the value.
Result<void, HeaderError> change_header(Log& log, HeaderField field, std::string new_value, Timestamp when, EntryId id);

/// Fixes the value currently in force: the latest live change of that field if there is one, otherwise the value the log
/// started with (which an empty text clears). Writes no entry. A change cannot be corrected to the value before it.
Result<void, HeaderError> correct_header(Log& log, HeaderField field, std::string value);

/// Fixes the new value of one change entry.
Result<void, HeaderError> correct_header_change(Log& log, EntryId change, std::string new_value);

}  // namespace ej
