// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "golden_scenarios.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/settings_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QString>
#include <QVariant>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace ej::test
{

/// A `SettingsModel` on the fakes: a fake `ConfigStore` behind a `ManualStorageGateway`, so a test looks at the model
/// between "asked" and "saved". Used by the tests of package 3.3.
struct SettingsRig
{
    TestEnvironment m_World;
    MemoryStorage m_Storage{m_World.m_Clock};
    ManualStorageGateway m_Gateway{m_Storage.context()};
    viewmodel::SettingsModel m_Settings{m_Gateway, m_World.environment()};
    FakeConfigurationObserver m_Observer;

    SettingsRig()
    {
        m_Settings.add_observer(m_Observer);
    }

    [[nodiscard]] FakeConfigStore& store()
    {
        return m_Storage.config();
    }

    /// Starts the model and lets the load finish.
    void load()
    {
        m_Settings.load();
        m_Gateway.run_all();
    }

    /// The file on disk is `text` (or there is none), then the model loads.
    void load_text(std::optional<std::string> text)
    {
        store().set_saved(std::move(text));
        load();
    }

    /// A configured model holding `configuration`, with the counters of the store and the observer at zero.
    void load_configuration(const Configuration& configuration)
    {
        load_text(export_configuration(configuration));
        REQUIRE(m_Settings.configured());
    }

    /// A configured model holding the sample configuration (`config/sample-config.json`).
    void load_sample()
    {
        load_configuration(sample_configuration());
    }

    /// Runs the queued save and the completion that follows.
    void commit()
    {
        REQUIRE(m_Gateway.run_next());
    }
};

/// The configuration of the rendering tests: the world of `golden_scenarios` (its ids, names and templates), with Echo
/// Delay retired, so a scenario made there renders through lookups into a configuration.
[[nodiscard]] inline Configuration scenario_configuration()
{
    Configuration configuration;
    configuration.m_Facilitator = "Sam Example";
    configuration.m_UnitPrefix = "UNIT";
    const std::pair<std::uint64_t, const char*> errors[] = {
        {ERROR_WRONG_RESPONSE, "Wrong Response"},         {ERROR_MUTE_DELAY, "Mute Delay"},
        {ERROR_WRONG_TRACK_PLAYED, "Wrong Track Played"}, {ERROR_SPEECH_DELAY, "Speech Delay"},
        {ERROR_LATE_GREETING, "Late Greeting"},           {ERROR_ECHO_DELAY_RETIRED, "Echo Delay"}};
    int position = 1;
    for (const auto& [number, name] : errors)
    {
        ErrorType type;
        type.m_Id = test_id<ErrorTypeId>(number);
        type.m_Name = name;
        type.m_Retired = number == ERROR_ECHO_DELAY_RETIRED;
        if (!type.m_Retired)
        {
            type.m_BoardPosition = position++;
        }
        configuration.m_ErrorTypes.push_back(std::move(type));
    }
    int order = 1;
    for (const auto& [number, name] : {std::pair<std::uint64_t, const char*>{LOCATION_NORTH_SITE, "North Site"},
                                       {LOCATION_SOUTH_SITE, "South Site"}})
    {
        Location location;
        location.m_Id = test_id<LocationId>(number);
        location.m_Name = name;
        location.m_SortOrder = order++;
        configuration.m_Locations.push_back(std::move(location));
    }
    order = 1;
    for (const auto& [number, name, pattern] :
         {std::tuple<std::uint64_t, const char*, const char*>{TEMPLATE_PLAYED, "Played song", "Played {value}"},
          {TEMPLATE_VISITOR, "Visitor", "Visitor interaction: {value}"},
          {TEMPLATE_REQUESTED, "Requested song", "Song {value} requested twice"}})
    {
        NoteTemplate note_template;
        note_template.m_Id = test_id<NoteTemplateId>(number);
        note_template.m_Name = name;
        note_template.m_ValuePrompt = "Which one?";
        note_template.m_OutputPattern = pattern;
        note_template.m_SortOrder = order++;
        configuration.m_Templates.push_back(std::move(note_template));
    }
    return configuration;
}

/// What `ExportContextBuilder` (work package 3.2) will do: the renderer's context, built from the configuration in
/// force. The context refers to `configuration`, which must not change while it is used.
[[nodiscard]] inline ExportContext context_from(const Configuration& configuration, const TimeZoneProvider& zones)
{
    ExportContext context;
    context.m_UnitPrefix = configuration.m_UnitPrefix;
    context.m_TimeFormat = configuration.m_TimeFormat;
    context.m_IncludeTotals = configuration.m_IncludeTotals;
    context.m_Zones = &zones;
    for (const ErrorType& type : configuration.m_ErrorTypes)
    {
        context.m_TotalsOrder.push_back(type.m_Id);
    }
    context.m_ErrorTypeName = [&configuration](ErrorTypeId id) -> std::string_view
    {
        for (const ErrorType& type : configuration.m_ErrorTypes)
        {
            if (type.m_Id == id)
            {
                return type.m_Name;
            }
        }
        return {};
    };
    context.m_LocationName = [&configuration](LocationId id) -> std::string_view
    {
        for (const Location& location : configuration.m_Locations)
        {
            if (location.m_Id == id)
            {
                return location.m_Name;
            }
        }
        return {};
    };
    context.m_Template = [&configuration](NoteTemplateId id) -> const NoteTemplate*
    {
        for (const NoteTemplate& note_template : configuration.m_Templates)
        {
            if (note_template.m_Id == id)
            {
                return &note_template;
            }
        }
        return nullptr;
    };
    return context;
}

/// The data of one cell of a model, as text.
[[nodiscard]] inline QString text_at(const QAbstractItemModel& model, int row, int role)
{
    return model.data(model.index(row, 0), role).toString();
}

[[nodiscard]] inline int number_at(const QAbstractItemModel& model, int row, int role)
{
    return model.data(model.index(row, 0), role).toInt();
}

/// The names of the roles, to find a role by the name QML uses.
[[nodiscard]] inline int role_named(const QAbstractItemModel& model, const char* name)
{
    return model.roleNames().key(QByteArray(name), -1);
}

} // namespace ej::test
