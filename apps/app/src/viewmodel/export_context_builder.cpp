// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/export_context_builder.hpp"

#include "error_journaling/configuration.hpp"
#include "viewmodel/export_preferences.hpp"
#include "viewmodel/person_names.hpp"

#include <algorithm>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ej::viewmodel
{

namespace
{

/// The lookups of one context: ids to the strings and templates of the configuration, indexed once so that rendering
/// a log of thousands of entries does not scan the lists for each. Shared by the context's functions.
struct Index
{
    std::unordered_map<ErrorTypeId, std::string_view> m_ErrorTypes;
    std::unordered_map<LocationId, std::string_view> m_Locations;
    std::unordered_map<NoteTemplateId, const NoteTemplate*> m_Templates;
    std::unordered_map<InteractionTypeId, const InteractionType*> m_InteractionTypes;
};

std::shared_ptr<const Index> make_index(const Configuration& configuration)
{
    auto index = std::make_shared<Index>();
    for (const ErrorType& type : configuration.m_ErrorTypes)
    {
        index->m_ErrorTypes.emplace(type.m_Id, type.m_Name);
    }
    for (const Location& location : configuration.m_Locations)
    {
        index->m_Locations.emplace(location.m_Id, location.m_Name);
    }
    for (const NoteTemplate& note_template : configuration.m_Templates)
    {
        index->m_Templates.emplace(note_template.m_Id, &note_template);
    }
    for (const InteractionType& type : configuration.m_InteractionTypes)
    {
        index->m_InteractionTypes.emplace(type.m_Id, &type);
    }
    return index;
}

/// Board order first (types with a board position that are not retired, by position), then the rest of the master list
/// in its order, retired types included so that a log that used one still totals it in a stable place.
std::vector<ErrorTypeId> totals_order(const Configuration& configuration)
{
    std::vector<const ErrorType*> on_board;
    for (const ErrorType& type : configuration.m_ErrorTypes)
    {
        if (!type.m_Retired && type.m_BoardPosition)
        {
            on_board.push_back(&type);
        }
    }
    std::ranges::stable_sort(on_board, [](const ErrorType* a, const ErrorType* b)
                             { return *a->m_BoardPosition < *b->m_BoardPosition; });

    std::vector<ErrorTypeId> order;
    order.reserve(configuration.m_ErrorTypes.size());
    for (const ErrorType* type : on_board)
    {
        order.push_back(type->m_Id);
    }
    for (const ErrorType& type : configuration.m_ErrorTypes)
    {
        if (std::ranges::find(order, type.m_Id) == order.end())
        {
            order.push_back(type.m_Id);
        }
    }
    return order;
}

} // namespace

ExportContext ExportContextBuilder::build() const
{
    // The configuration is empty while it is not configured, so nothing resolves and the prefix is empty.
    const Configuration& configuration = m_Configuration.configuration();
    const std::shared_ptr<const Index> index = make_index(configuration);

    ExportContext context;
    context.m_UnitPrefix = configuration.m_UnitPrefix;
    context.m_ErrorPrefix = configuration.m_ErrorPrefix;
    context.m_TimeFormat = configuration.m_TimeFormat;
    context.m_IncludeTotals = configuration.m_IncludeTotals;
    context.m_Zones = &m_Zones;
    context.m_TotalsOrder = totals_order(configuration);
    context.m_ErrorTypeName = [index](ErrorTypeId id) -> std::string_view
    {
        const auto it = index->m_ErrorTypes.find(id);
        return it == index->m_ErrorTypes.end() ? std::string_view{} : it->second;
    };
    context.m_LocationName = [index](LocationId id) -> std::string_view
    {
        const auto it = index->m_Locations.find(id);
        return it == index->m_Locations.end() ? std::string_view{} : it->second;
    };
    context.m_InteractionTypeName = [index](InteractionTypeId id) -> std::string_view
    {
        const auto it = index->m_InteractionTypes.find(id);
        return it == index->m_InteractionTypes.end() ? std::string_view{} : std::string_view(it->second->m_Name);
    };
    context.m_InteractionPattern = [index](InteractionTypeId id) -> std::optional<std::string>
    {
        const auto it = index->m_InteractionTypes.find(id);
        return it == index->m_InteractionTypes.end() ? std::nullopt : it->second->m_LinePattern;
    };
    if (m_People != nullptr)
    {
        const PersonNames* const people = m_People;
        context.m_PersonName = [people](const PersonId& id) { return people->name_of(id); };
    }
    if (m_Preferences != nullptr)
    {
        context.m_InteractionMarker = m_Preferences->marker();
    }
    // The screens show the full name; `build_for_sharing` lowers it.
    context.m_NameLevel = NameLevel::FULL;
    context.m_Template = [index](NoteTemplateId id) -> const NoteTemplate*
    {
        const auto it = index->m_Templates.find(id);
        return it == index->m_Templates.end() ? nullptr : it->second;
    };
    return context;
}

ExportContext ExportContextBuilder::build_for_sharing() const
{
    ExportContext context = build();
    context.m_NameLevel = m_Preferences != nullptr ? m_Preferences->name_level() : NameLevel::INITIALS;
    return context;
}

Result<std::string, RenderError> export_text(const Log& log, const ExportContext& context)
{
    return render_export(log, context);
}

} // namespace ej::viewmodel
