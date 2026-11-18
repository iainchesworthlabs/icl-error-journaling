// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/journal_record.hpp"

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// Sample data for the storage tests, built only from the fictional stand-ins (Sam Example, UNIT, North Site, Wrong
/// Response). Shared by the core tests of the journal format and the storage tests.
namespace ej::test
{

template <typename Id>
[[nodiscard]] Id sample_id(std::uint64_t number)
{
    return Id(Uuid::from_random(0, number));
}

/// 7 Nov 2026 00:00 UTC plus `minute` minutes.
[[nodiscard]] inline UtcMillis sample_time(std::int64_t minute)
{
    return std::chrono::sys_days{std::chrono::year{2026} / 11 / 7} + std::chrono::minutes(minute);
}

[[nodiscard]] inline HeaderValues sample_header()
{
    HeaderValues header;
    header.m_Tester = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    header.m_UnitNumber = UnitNumber::parse("27").value();
    header.m_Location = LocationValue{std::nullopt, "North Site"};
    header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2").value();
    return header;
}

[[nodiscard]] inline Entry sample_error(std::uint64_t number, std::int64_t minute, std::string label = "Wrong Response",
                                        std::optional<std::string> note = std::nullopt)
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(number);
    entry.m_Captured = Timestamp{sample_time(minute), "Australia/Melbourne"};
    entry.m_Body = ErrorEntry{sample_id<ErrorTypeId>(1), std::move(note), std::move(label)};
    return entry;
}

/// An interaction entry of the type "Spoke with" (type id 1 of the interaction types), with the people it names by id.
/// The label is a snapshot, as an error entry's is; there is no name anywhere in it.
[[nodiscard]] inline Entry sample_interaction_entry(std::uint64_t number, std::int64_t minute,
                                                    std::vector<PersonId> people = {},
                                                    std::optional<std::string> value = std::nullopt,
                                                    std::optional<std::string> note = std::nullopt,
                                                    std::string label = "Spoke with")
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(number);
    entry.m_Captured = Timestamp{sample_time(minute), "Australia/Melbourne"};
    entry.m_Body = InteractionEntry{sample_id<InteractionTypeId>(1), std::move(label), std::move(people),
                                    std::move(value), std::move(note)};
    return entry;
}

[[nodiscard]] inline Entry sample_note(std::uint64_t number, std::int64_t minute, std::string text)
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(number);
    entry.m_Captured = Timestamp{sample_time(minute), "Australia/Melbourne"};
    entry.m_Body = NoteEntry{std::nullopt, std::move(text)};
    return entry;
}

[[nodiscard]] inline Entry sample_change(std::uint64_t number, std::int64_t minute, HeaderField field,
                                         std::string value)
{
    Entry entry;
    entry.m_Id = sample_id<EntryId>(number);
    entry.m_Captured = Timestamp{sample_time(minute), "Australia/Melbourne"};
    entry.m_Body = HeaderChangeEntry{field, std::move(value)};
    return entry;
}

[[nodiscard]] inline JournalRecord sample_record(std::uint64_t seq, JournalBody body, std::uint64_t log = 100)
{
    JournalRecord record;
    record.m_Seq = seq;
    record.m_Log = sample_id<LogId>(log);
    record.m_Body = std::move(body);
    return record;
}

}  // namespace ej::test
