// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a person's name is written where it leaves the device (Stage 14, 3.6; T-I4-2). Share, Copy line and Copy all use
// the preference `name_export`; Review and the board show the full name.
#include "error_journaling/configuration.hpp"
#include "fake_time_zone_provider.hpp"
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/export_context_builder.hpp"
#include "viewmodel/export_preferences.hpp"
#include "viewmodel/person_names.hpp"
#include "viewmodel/timeline_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::ExportContextBuilder;
using viewmodel::ExportPreferences;
using viewmodel::LogDocument;
using viewmodel::PersonNames;
using viewmodel::TimelineModel;

namespace
{

/// The fictional list of AGENTS.md; a person who is not in it reads as forgotten.
class FixedPeople final : public PersonNames
{
public:
    FixedPeople()
    {
        m_Names[test_id<PersonId>(PERSON_ALEX)] = "Alex Sample";
        m_Names[test_id<PersonId>(PERSON_JORDAN)] = "Jordan Placeholder";
    }

    [[nodiscard]] std::optional<std::string> name_of(const PersonId& id) const override
    {
        const auto it = m_Names.find(id);
        return it == m_Names.end() ? std::nullopt : std::optional<std::string>(it->second);
    }

private:
    std::map<PersonId, std::string> m_Names;
};

/// The sample configuration with the interaction type of the examples.
Configuration configuration_with_interactions()
{
    Configuration configuration = sample_configuration();
    InteractionType spoke;
    spoke.m_Id = test_id<InteractionTypeId>(INTERACTION_SPOKE_WITH);
    spoke.m_Name = "Spoke with";
    spoke.m_LinePattern = "Spoke with {person}";
    spoke.m_Asks = InteractionAsks::PERSON;
    configuration.m_InteractionTypes = {spoke};
    return configuration;
}

Entry error_at(std::uint64_t number, int hour, int minute)
{
    Entry entry = sample_error(number, 0, "Wrong Response", std::nullopt);
    entry.m_Captured = on_day(hour, minute);
    return entry;
}

/// A timeline over a log with an error and two interactions, over a people list and a preference that is in memory.
struct Fixture
{
    Fixture()
    {
        m_Config.set(configuration_with_interactions());
        m_World.m_People = &m_People;
        m_World.m_ExportPreferences = &m_Preferences;
        const LogId id = sample_id<LogId>(1);
        REQUIRE(m_Storage.memory().create_log(id, sample_header()));
        const std::vector<Entry> entries = {
            error_at(1, 10, 5), interaction_entry(2, on_day(10, 12), INTERACTION_SPOKE_WITH, {PERSON_ALEX}),
            interaction_entry(3, on_day(10, 40), INTERACTION_SPOKE_WITH, {PERSON_ALEX, PERSON_JORDAN})};
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        m_Document = std::make_shared<LogDocument>(m_World.environment(), m_Gateway, std::move(*loaded));
        m_Model = std::make_unique<TimelineModel>(m_World.environment(), m_Config, m_Clipboard);
        m_Model->bind(m_Document);
    }

    [[nodiscard]] ExportContextBuilder builder()
    {
        return ExportContextBuilder(m_Config, m_World.environment());
    }

    TestEnvironment m_World;
    FixedPeople m_People;
    ExportPreferences m_Preferences{QString()};
    EventLog m_Events;
    MemoryStorage m_Storage{m_World.m_Clock, &m_Events};
    ManualStorageGateway m_Gateway{m_Storage.context()};
    FakeConfigurationSource m_Config;
    FakeClipboard m_Clipboard{&m_Events};
    std::shared_ptr<LogDocument> m_Document;
    std::unique_ptr<TimelineModel> m_Model;
};

} // namespace

TEST_CASE("export.name_levels: the screen shows the full name, sharing writes the chosen level",
          "[viewmodel][export][names]")
{
    Fixture fixture;
    const Log& log = fixture.m_Document->log();

    const auto screen = viewmodel::export_text(log, fixture.builder().build());
    REQUIRE(screen.has_value());
    CHECK(screen->find("10:12 (Interaction) Spoke with Alex Sample\n") != std::string::npos);
    CHECK(screen->find("Spoke with Alex Sample and Jordan Placeholder") != std::string::npos);

    // The default is initials.
    CHECK(fixture.m_Preferences.name_export() == "initials");
    const auto initials = viewmodel::export_text(log, fixture.builder().build_for_sharing());
    REQUIRE(initials.has_value());
    CHECK(initials->find("10:12 (Interaction) Spoke with A.S.\n") != std::string::npos);
    CHECK(initials->find("10:40 (Interaction) Spoke with A.S. and J.P.") != std::string::npos);
    CHECK(initials->find("Alex") == std::string::npos);
    CHECK(initials->find("Jordan") == std::string::npos);
    CHECK(initials->find("10:05 T| Wrong Response\n") != std::string::npos);
    CHECK(initials->find("(Interaction) T|") == std::string::npos);

    fixture.m_Preferences.set_name_export("hidden");
    const auto hidden = viewmodel::export_text(log, fixture.builder().build_for_sharing());
    REQUIRE(hidden.has_value());
    CHECK(hidden->find("10:12 (Interaction) Spoke with someone\n") != std::string::npos);
    CHECK(hidden->find("10:40 (Interaction) Spoke with someone") != std::string::npos);

    fixture.m_Preferences.set_name_export("full");
    const auto full = viewmodel::export_text(log, fixture.builder().build_for_sharing());
    REQUIRE(full.has_value());
    CHECK(*full == *screen);

    // A word that is not a level changes nothing.
    fixture.m_Preferences.set_name_export("everyone");
    CHECK(fixture.m_Preferences.name_export() == "full");
}

TEST_CASE("export.name_levels: Copy line and Copy all follow the setting, Review does not",
          "[viewmodel][export][names]")
{
    Fixture fixture;
    TimelineModel& model = *fixture.m_Model;
    REQUIRE(model.rowCount() == 3);

    // Review: the rows keep the full name whatever the setting is.
    CHECK(model.data(model.index(2), TimelineModel::FULL_LINE).toString() ==
          QStringLiteral("10:40 (Interaction) Spoke with Alex Sample and Jordan Placeholder"));

    REQUIRE(model.copyLine(2));
    CHECK(fixture.m_Clipboard.text() == "10:40 (Interaction) Spoke with A.S. and J.P.");
    REQUIRE(model.copyLine(0));
    CHECK(fixture.m_Clipboard.text() == "10:05 T| Wrong Response"); // an error line is untouched

    REQUIRE(model.copyAll());
    CHECK(fixture.m_Clipboard.text().find("Alex") == std::string::npos);
    CHECK(fixture.m_Clipboard.text().find("\n10:12 (Interaction) Spoke with A.S.\n") != std::string::npos);

    // The setting is read at the time of the copy: a log that is copied again uses the current level.
    fixture.m_Preferences.set_name_export("full");
    REQUIRE(model.copyLine(1));
    CHECK(fixture.m_Clipboard.text() == "10:12 (Interaction) Spoke with Alex Sample");
    fixture.m_Preferences.set_name_export("hidden");
    REQUIRE(model.copyAll());
    CHECK(fixture.m_Clipboard.text().find("Spoke with someone") != std::string::npos);
    CHECK(fixture.m_Clipboard.text().find("Sample") == std::string::npos);
}

TEST_CASE("export.name_levels: the label is the preference", "[viewmodel][export][names]")
{
    Fixture fixture;
    fixture.m_Preferences.set_interaction_marker("(Chat)");
    auto text = viewmodel::export_text(fixture.m_Document->log(), fixture.builder().build_for_sharing());
    REQUIRE(text.has_value());
    CHECK(text->find("10:12 (Chat) Spoke with A.S.\n") != std::string::npos);

    fixture.m_Preferences.set_interaction_marker("");
    text = viewmodel::export_text(fixture.m_Document->log(), fixture.builder().build_for_sharing());
    REQUIRE(text.has_value());
    CHECK(text->find("10:12 Spoke with A.S.\n") != std::string::npos);

    // Too long, and with a line break: cut, so a label is always one short word on the line.
    fixture.m_Preferences.set_interaction_marker("  (A very long label indeed, longer than any)\nsecond line");
    CHECK(fixture.m_Preferences.interaction_marker().size() <= ExportPreferences::MAX_MARKER_CHARACTERS);
    CHECK(fixture.m_Preferences.interaction_marker().indexOf(QLatin1Char('\n')) < 0);
}

TEST_CASE("export.name_levels: without a list of people nobody is named", "[viewmodel][export][names]")
{
    Fixture fixture;
    fixture.m_World.m_People = nullptr;
    const auto text = viewmodel::export_text(fixture.m_Document->log(), fixture.builder().build());
    REQUIRE(text.has_value());
    CHECK(text->find("10:12 (Interaction) Spoke with someone\n") != std::string::npos);
}
