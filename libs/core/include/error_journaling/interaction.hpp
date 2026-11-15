// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/entry.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/person.hpp"
#include "error_journaling/result.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ej
{

/// One interaction names at most this many people ("Everyone" at a location is a pick of its people).
inline constexpr std::size_t MAX_PEOPLE_PER_INTERACTION = 20;

/// What the app asks for after an interaction tile is tapped. Derived from the pattern and kept equal to it.
enum class InteractionAsks
{
    NOTHING,
    PERSON,
    TEXT
};

[[nodiscard]] std::string_view interaction_asks_key(InteractionAsks asks);
[[nodiscard]] std::optional<InteractionAsks> interaction_asks_from_key(std::string_view key);

/// A tile of the second board. The master list is defined like the error types: position 1 to 11 or none, retired
/// types keep their history.
struct InteractionType
{
    InteractionTypeId m_Id;
    std::string m_Name;
    bool m_Retired = false;
    std::optional<int> m_BoardPosition;
    /// The line as the log shows it; may hold {person} and {value}, at most one of each. Nothing renders the name.
    std::optional<std::string> m_LinePattern;
    InteractionAsks m_Asks = InteractionAsks::NOTHING;
};

enum class InteractionPatternError
{
    EMPTY,
    UNBALANCED_BRACE,
    UNKNOWN_PLACEHOLDER,
    REPEATED_PLACEHOLDER
};

/// The placeholders are {person} and {value}, each at most once; a literal brace is written {{ or }}. A pattern with no
/// placeholder is allowed (a fixed line).
[[nodiscard]] Result<void, InteractionPatternError> validate_interaction_pattern(std::string_view pattern);

/// `person` when the pattern holds {person} (also when it holds both: the person is asked for first), else `text` when
/// it holds {value}, else `nothing`. No pattern is `nothing`.
[[nodiscard]] InteractionAsks interaction_asks_for(const std::optional<std::string>& pattern);

/// The words of an interaction's line: `people_text` is the result of `render_people`, which has already applied the
/// name level. With no pattern the line is the type's name. `{value}` is the value trimmed of white space and reads
/// empty when none is set. Total: a pattern that does not validate keeps its stray braces as they are.
[[nodiscard]] std::string render_interaction_text(const std::optional<std::string>& pattern,
                                                  std::string_view type_label, std::string_view people_text,
                                                  const std::optional<std::string>& value);

enum class PickError
{
    TOO_MANY_PEOPLE
};

/// Appends `person` to the picks. A person already there is ignored (the order of the first pick stays). Refuses the
/// 21st different person.
[[nodiscard]] Result<void, PickError> add_person_pick(std::vector<PersonId>& people, PersonId person);

}  // namespace ej
