// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/export_renderer.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/log.hpp"

namespace ej::test
{

/// A deterministic id for a test: the same number always gives the same id.
template <typename Id>
[[nodiscard]] Id test_id(std::uint64_t number)
{
    return Id(Uuid::from_random(0, number));
}

/// A log and the context that renders it. The context's functions and zone provider live in `m_World`, which the
/// scenario keeps alive, so a scenario can be copied and returned freely.
struct Scenario
{
    Log m_Log;
    ExportContext m_Context;
    std::shared_ptr<void> m_World;
};

/// The names of the golden files, each built from the scenario of the same name. The files are in `golden/<name>.txt`.
[[nodiscard]] const std::vector<std::string_view>& golden_names();

/// Builds the scenario behind golden `name` (fictional data only: Sam Example, UNIT, North Site, Wrong Response).
[[nodiscard]] Scenario make_scenario(std::string_view name);

/// A log with the standard header (Sam Example, 7 Nov 2026, UNIT-27, North Site, 1.4.2), no entries, and the standard
/// world: fixed-offset zones for Australia/Melbourne (+11), Australia/Perth (+8) and UTC.
[[nodiscard]] Scenario blank_scenario();

/// 7 Nov 2026 in the given zone of the standard world (Melbourne by default), as a capture timestamp.
[[nodiscard]] Timestamp on_day(int hour, int minute, int second = 0, int millisecond = 0,
                               std::string_view zone = "Australia/Melbourne");

[[nodiscard]] Entry error_entry(std::uint64_t id, Timestamp when, std::uint64_t error_type,
                                std::optional<std::string> attached_note = std::nullopt);
[[nodiscard]] Entry free_note_entry(std::uint64_t id, Timestamp when, std::string text);
[[nodiscard]] Entry template_note_entry(std::uint64_t id, Timestamp when, std::uint64_t template_number,
                                        std::string value);

/// The fictional names of the scenario world, for tests that build their own logs.
inline constexpr std::uint64_t ERROR_WRONG_RESPONSE = 1;
inline constexpr std::uint64_t ERROR_MUTE_DELAY = 2;
inline constexpr std::uint64_t ERROR_WRONG_TRACK_PLAYED = 3;
inline constexpr std::uint64_t ERROR_SPEECH_DELAY = 4;
inline constexpr std::uint64_t ERROR_LATE_GREETING = 5;
inline constexpr std::uint64_t ERROR_ECHO_DELAY_RETIRED = 6;
inline constexpr std::uint64_t LOCATION_NORTH_SITE = 1;
inline constexpr std::uint64_t LOCATION_SOUTH_SITE = 2;
inline constexpr std::uint64_t TEMPLATE_PLAYED = 1;
inline constexpr std::uint64_t TEMPLATE_VISITOR = 2;
inline constexpr std::uint64_t TEMPLATE_REQUESTED = 3;

}  // namespace ej::test
