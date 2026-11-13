// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/timestamp.hpp"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace ej
{

/// A name is at most this many code points, after trimming and collapsing.
inline constexpr std::size_t MAX_PERSON_NAME_CODE_POINTS = 60;

/// One entry of the optional name list. The name list lives in its own store, never in a log (feedback page, 3.4).
struct Person
{
    PersonId m_Id;
    std::string m_DisplayName; // as typed, after `normalise_person_name`
    bool m_Retired = false;
    std::optional<LocationId> m_Location;
    std::optional<UtcMillis> m_LastUsedUtc;
    UtcMillis m_CreatedUtc;
};

enum class NameError
{
    EMPTY,
    TOO_LONG,
    NOT_UTF8
};

/// Trims, collapses every run of white space inside to one space and checks the length. The name keeps the case and
/// accents it was typed with. White space is the ASCII white space, NEL, the no-break space, the Unicode spaces and the
/// line and paragraph separators.
[[nodiscard]] Result<std::string, NameError> normalise_person_name(std::string_view typed);

/// The text two names are compared by: normalised, accents removed, case folded. "alex  SAMPLE" and "Alex Sample" have
/// the same key, and so do "José" and "Jose". Empty when the name does not normalise.
[[nodiscard]] std::string person_name_key(std::string_view name);

[[nodiscard]] bool person_names_match(std::string_view a, std::string_view b);

/// How a person's name is written in text that leaves the device (`name_export`).
enum class NameLevel
{
    FULL,
    INITIALS,
    HIDDEN
};

[[nodiscard]] std::string_view name_level_key(NameLevel level);
[[nodiscard]] std::optional<NameLevel> name_level_from_key(std::string_view key);

/// What stands for any name at the `hidden` level, and for a person whose name is gone (Forget).
inline constexpr std::string_view HIDDEN_NAME = "someone";
/// What an interaction's {person} slot reads when nobody is set.
inline constexpr std::string_view PERSON_NOT_SET = "(person not set)";

/// "Alex Sample" is "A.S."; "Madonna" is "M."; "Mary-Jane Watson" is "M.J.W.". Words are split at white space and at
/// hyphens; the first letter of each is upper-cased.
[[nodiscard]] std::string person_initials(std::string_view name);

/// One person's name at a level. `hidden` is "someone" for every name.
[[nodiscard]] std::string person_name_at_level(std::string_view name, NameLevel level);

/// The text of a {person} slot: nobody gives `PERSON_NOT_SET`; one name is "A", two are "A and B", more are
/// "A, B and C", in the order given. A person with no name (forgotten, or not found) is `nullopt` and reads "someone",
/// as at the `hidden` level, where the whole slot reads "someone" however many there are.
[[nodiscard]] std::string render_people(std::span<const std::optional<std::string>> names, NameLevel level);

}  // namespace ej
