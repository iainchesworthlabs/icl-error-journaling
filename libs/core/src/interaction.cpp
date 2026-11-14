// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/interaction.hpp"

#include <algorithm>

namespace ej
{

namespace
{

constexpr std::string_view PERSON_SLOT = "{person}";
constexpr std::string_view VALUE_SLOT = "{value}";

bool is_ascii_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

std::string_view trim(std::string_view text)
{
    while (!text.empty() && is_ascii_space(text.front()))
    {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_ascii_space(text.back()))
    {
        text.remove_suffix(1);
    }
    return text;
}

struct Expansion
{
    std::string m_Text;
    std::size_t m_People = 0;
    std::size_t m_Values = 0;
    std::optional<InteractionPatternError> m_Problem; // the first structural problem; the scan carries on
};

/// One scanner for validation, the question and rendering, so they cannot disagree about what a pattern means.
Expansion expand(std::string_view pattern, std::string_view people, std::string_view value)
{
    Expansion result;
    const auto problem = [&result](InteractionPatternError error)
    {
        if (!result.m_Problem)
        {
            result.m_Problem = error;
        }
    };
    for (std::size_t i = 0; i < pattern.size();)
    {
        const char c = pattern[i];
        if ((c == '{' || c == '}') && i + 1 < pattern.size() && pattern[i + 1] == c)
        {
            result.m_Text += c;
            i += 2;
        }
        else if (c == '{')
        {
            if (pattern.substr(i, PERSON_SLOT.size()) == PERSON_SLOT)
            {
                result.m_Text += people;
                if (++result.m_People > 1)
                {
                    problem(InteractionPatternError::REPEATED_PLACEHOLDER);
                }
                i += PERSON_SLOT.size();
            }
            else if (pattern.substr(i, VALUE_SLOT.size()) == VALUE_SLOT)
            {
                result.m_Text += value;
                if (++result.m_Values > 1)
                {
                    problem(InteractionPatternError::REPEATED_PLACEHOLDER);
                }
                i += VALUE_SLOT.size();
            }
            else
            {
                problem(pattern.find('}', i) == std::string_view::npos ? InteractionPatternError::UNBALANCED_BRACE
                                                                       : InteractionPatternError::UNKNOWN_PLACEHOLDER);
                result.m_Text += c;
                ++i;
            }
        }
        else
        {
            if (c == '}')
            {
                problem(InteractionPatternError::UNBALANCED_BRACE);
            }
            result.m_Text += c;
            ++i;
        }
    }
    return result;
}

}  // namespace

std::string_view interaction_asks_key(InteractionAsks asks)
{
    switch (asks)
    {
    case InteractionAsks::NOTHING:
        return "nothing";
    case InteractionAsks::PERSON:
        return "person";
    case InteractionAsks::TEXT:
        return "text";
    }
    return {};
}

std::optional<InteractionAsks> interaction_asks_from_key(std::string_view key)
{
    for (const InteractionAsks asks : {InteractionAsks::NOTHING, InteractionAsks::PERSON, InteractionAsks::TEXT})
    {
        if (interaction_asks_key(asks) == key)
        {
            return asks;
        }
    }
    return std::nullopt;
}

Result<void, InteractionPatternError> validate_interaction_pattern(std::string_view pattern)
{
    if (pattern.empty())
    {
        return fail(InteractionPatternError::EMPTY);
    }
    if (const auto expansion = expand(pattern, {}, {}); expansion.m_Problem)
    {
        return fail(*expansion.m_Problem);
    }
    return {};
}

InteractionAsks interaction_asks_for(const std::optional<std::string>& pattern)
{
    if (!pattern)
    {
        return InteractionAsks::NOTHING;
    }
    const auto expansion = expand(*pattern, {}, {});
    if (expansion.m_People != 0)
    {
        return InteractionAsks::PERSON;
    }
    return expansion.m_Values != 0 ? InteractionAsks::TEXT : InteractionAsks::NOTHING;
}

std::string render_interaction_text(const std::optional<std::string>& pattern, std::string_view type_label,
                                    std::string_view people_text, const std::optional<std::string>& value)
{
    if (!pattern)
    {
        return std::string(type_label);
    }
    return expand(*pattern, people_text, value ? trim(*value) : std::string_view{}).m_Text;
}

std::vector<InteractionType> default_interaction_types()
{
    struct Row
    {
        const char* m_Id;
        const char* m_Name;
        const char* m_Pattern;
    };
    static constexpr Row ROWS[] = {
        {"00000000-0000-4000-8000-0000000000d1", "Spoke with", "Spoke with {person}"},
        {"00000000-0000-4000-8000-0000000000d2", "Played song", "Played {value}"},
        {"00000000-0000-4000-8000-0000000000d3", "Visitor interaction", nullptr},
        {"00000000-0000-4000-8000-0000000000d4", "Gave item", "Gave {person} {value}"},
        {"00000000-0000-4000-8000-0000000000d5", "Greeted", nullptr},
        {"00000000-0000-4000-8000-0000000000d6", "Helped", "Helped {person}"},
    };
    std::vector<InteractionType> types;
    int position = 1;
    for (const Row& row : ROWS)
    {
        InteractionType type;
        type.m_Id = InteractionTypeId{*Uuid::parse(row.m_Id)};
        type.m_Name = row.m_Name;
        type.m_BoardPosition = position++;
        if (row.m_Pattern != nullptr)
        {
            type.m_LinePattern = row.m_Pattern;
        }
        type.m_Asks = interaction_asks_for(type.m_LinePattern);
        types.push_back(std::move(type));
    }
    return types;
}

Result<void, PickError> add_person_pick(std::vector<PersonId>& people, PersonId person)
{
    if (std::ranges::find(people, person) != people.end())
    {
        return {};
    }
    if (people.size() >= MAX_PEOPLE_PER_INTERACTION)
    {
        return fail(PickError::TOO_MANY_PEOPLE);
    }
    people.push_back(person);
    return {};
}

}  // namespace ej
