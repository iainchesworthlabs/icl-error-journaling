// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden_scenarios.hpp"

#include <chrono>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace ej::test
{

namespace
{

using namespace std::chrono_literals;

constexpr std::uint64_t NORTH_SITE = 1;

// Error type numbers of the scenario.
constexpr std::uint64_t WRONG_RESPONSE = 1;
constexpr std::uint64_t MUTE_DELAY = 2;
constexpr std::uint64_t WRONG_TRACK_PLAYED = 3;
constexpr std::uint64_t SPEECH_DELAY = 4;

/// A Melbourne wall-clock time on 7 Nov 2026 (UTC+11) as a timestamp.
Timestamp melbourne(std::chrono::hours hour, std::chrono::minutes minute)
{
    const auto local_midnight_utc = std::chrono::sys_days{std::chrono::year{2026} / 11 / 7} - 11h;
    return Timestamp{std::chrono::time_point_cast<std::chrono::milliseconds>(local_midnight_utc + hour + minute),
                     "Australia/Melbourne"};
}

Entry error_entry(std::uint64_t id, Timestamp when, std::uint64_t error_type, std::optional<std::string> note = {})
{
    Entry entry;
    entry.m_Id = test_id<EntryId>(id);
    entry.m_Captured = std::move(when);
    entry.m_Body = ErrorEntry{test_id<ErrorTypeId>(error_type), std::move(note)};
    return entry;
}

void must_add(Log& log, Entry entry)
{
    const auto added = log.add(std::move(entry));
    if (!added)
    {
        std::abort();  // a scenario with a duplicate id is a bug in the test data
    }
}

}  // namespace

Log prd_example_log()
{
    HeaderValues header;
    header.m_Facilitator = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    header.m_UnitNumber = UnitNumber::parse("27").value();
    header.m_Location = LocationValue{test_id<LocationId>(NORTH_SITE), {}};
    header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2").value();

    Log log(std::move(header));
    must_add(log, error_entry(1, melbourne(10h, 5min), WRONG_RESPONSE));
    must_add(log, error_entry(2, melbourne(10h, 20min), MUTE_DELAY));

    Entry note;
    note.m_Id = test_id<EntryId>(3);
    note.m_Captured = melbourne(10h, 30min);
    note.m_Body = NoteEntry{test_id<NoteTemplateId>(1), "Morning Medley"};
    must_add(log, std::move(note));

    must_add(log, error_entry(4, melbourne(11h, 2min), WRONG_TRACK_PLAYED, "asked for Evening Song"));

    Entry change;
    change.m_Id = test_id<EntryId>(5);
    change.m_Captured = melbourne(13h, 15min);
    change.m_Body = HeaderChangeEntry{HeaderField::SOFTWARE_VERSION, "1.5.0"};
    must_add(log, std::move(change));

    must_add(log, error_entry(6, melbourne(13h, 40min), SPEECH_DELAY));
    return log;
}

ExportContext prd_example_context()
{
    ExportContext context;
    context.m_UnitPrefix = "UNIT";
    context.m_LocationName = [](LocationId id) -> std::string_view
    { return id == test_id<LocationId>(NORTH_SITE) ? std::string_view("North Site") : std::string_view(); };
    return context;
}

}  // namespace ej::test
