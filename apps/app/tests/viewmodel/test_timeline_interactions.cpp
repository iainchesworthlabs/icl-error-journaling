// SPDX-License-Identifier: GPL-3.0-or-later
//
// Interaction entries in Review and in the Edit entry sheet (Stage 14, 4.6; T-I9-1, T-I9-2): the rows read as the
// export does, at full names; an entry can be changed afterwards (type, people, answer, time, note), also to give the
// people to an entry that was logged without; a deleted interaction does not count.
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
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::ExportPreferences;
using viewmodel::LogDocument;
using viewmodel::PersonNames;
using viewmodel::TimelineModel;

namespace
{

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

InteractionType make_type(std::uint64_t number, std::string name, std::optional<std::string> pattern,
                          InteractionAsks asks, bool retired = false)
{
    InteractionType type;
    type.m_Id = test_id<InteractionTypeId>(number);
    type.m_Name = std::move(name);
    type.m_LinePattern = std::move(pattern);
    type.m_Asks = asks;
    type.m_Retired = retired;
    return type;
}

Configuration configuration_with_interactions()
{
    Configuration configuration = sample_configuration();
    configuration.m_InteractionTypes = {
        make_type(INTERACTION_SPOKE_WITH, "Spoke with", "Spoke with {person}", InteractionAsks::PERSON),
        make_type(INTERACTION_GAVE, "Gave", "Gave {value}", InteractionAsks::TEXT),
        make_type(INTERACTION_GREETED, "Greeted", std::nullopt, InteractionAsks::NOTHING),
        make_type(INTERACTION_VISITOR, "Visitor", std::nullopt, InteractionAsks::NOTHING, true)};
    return configuration;
}

QString id_text(std::uint64_t number)
{
    return QString::fromStdString(sample_id<EntryId>(number).format());
}

QString type_text(std::uint64_t number)
{
    return QString::fromStdString(test_id<InteractionTypeId>(number).format());
}

QString person_text(std::uint64_t number)
{
    return QString::fromStdString(test_id<PersonId>(number).format());
}

/// Entries 1 to 6, in time order: spoke with Alex; spoke with nobody (skipped); gave a toy with a note; an error; spoke
/// with a forgotten person; greeted.
struct Fixture
{
    Fixture()
    {
        m_Config.set(configuration_with_interactions());
        m_World.m_People = &m_People;
        m_World.m_ExportPreferences = &m_Preferences;
        const LogId id = sample_id<LogId>(1);
        REQUIRE(m_Storage.memory().create_log(id, sample_header()));
        Entry error = sample_error(4, 0, "Wrong Response", std::nullopt);
        error.m_Captured = on_day(10, 30);
        const std::vector<Entry> entries = {
            interaction_entry(1, on_day(10, 10), INTERACTION_SPOKE_WITH, {PERSON_ALEX}),
            interaction_entry(2, on_day(10, 20), INTERACTION_SPOKE_WITH, {}),
            interaction_entry(3, on_day(10, 25), INTERACTION_GAVE, {}, "a toy", "was pleased"),
            error,
            interaction_entry(5, on_day(10, 40), INTERACTION_SPOKE_WITH, {PERSON_FORGOTTEN}),
            interaction_entry(6, on_day(10, 50), INTERACTION_GREETED, {})};
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

    [[nodiscard]] TimelineModel& model()
    {
        return *m_Model;
    }

    [[nodiscard]] QString line(int row)
    {
        return m_Model->data(m_Model->index(row), TimelineModel::FULL_LINE).toString();
    }

    void commit()
    {
        m_Gateway.run_all();
    }

    [[nodiscard]] QVariantMap edit_of(std::uint64_t number)
    {
        return m_Model->entryForEdit(id_text(number));
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

}  // namespace

TEST_CASE("review.interaction_rows: rows read as the export does, with the full name",
          "[viewmodel][review][interaction]")
{
    Fixture f;
    REQUIRE(f.model().rowCount() == 6);
    CHECK(f.line(0) == QStringLiteral("10:10 (Interaction) Spoke with Alex Sample"));
    CHECK(f.line(1) == QStringLiteral("10:20 (Interaction) Spoke with (person not set)"));
    CHECK(f.line(2) == QStringLiteral("10:25 (Interaction) Gave a toy – was pleased"));
    CHECK(f.line(3) == QStringLiteral("10:30 T| Wrong Response"));
    CHECK(f.line(4) == QStringLiteral("10:40 (Interaction) Spoke with someone"));
    CHECK(f.line(5) == QStringLiteral("10:50 (Interaction) Greeted"));
    CHECK(f.model().data(f.model().index(0), TimelineModel::KIND).toString() == "interaction");

    // The screen row is the export line at the full level: the same text, not another rendering.
    const viewmodel::ExportContextBuilder builder(f.m_Config, f.m_World.environment());
    const auto text = viewmodel::export_text(f.m_Document->log(), builder.build());
    REQUIRE(text.has_value());
    for (int row = 0; row < f.model().rowCount(); ++row)
    {
        CHECK(text->find(f.line(row).toStdString()) != std::string::npos);
    }
}

TEST_CASE("review.interaction_rows: copy follows the sharing level; a deleted interaction does not count",
          "[viewmodel][review][interaction]")
{
    Fixture f;
    REQUIRE(f.model().copyLine(0));
    CHECK(f.m_Clipboard.text() == "10:10 (Interaction) Spoke with A.S.");

    const int before = f.model().property("entryCount").toInt();
    REQUIRE(f.model().deleteEntry(id_text(1)));
    f.commit();
    CHECK(f.model().rowCount() == 5);
    CHECK(f.model().property("entryCount").toInt() == before - 1);
    CHECK(f.line(0).startsWith("10:20"));
}

TEST_CASE("edit.interaction: the values the sheet starts from", "[viewmodel][edit][interaction]")
{
    Fixture f;
    const QVariantMap alex = f.edit_of(1);
    CHECK(alex.value("kind").toString() == "interaction");
    CHECK(alex.value("interactionTypeId").toString() == type_text(INTERACTION_SPOKE_WITH));
    CHECK(alex.value("interactionTypeName").toString() == "Spoke with");
    CHECK(alex.value("personIds").toStringList() == QStringList{person_text(PERSON_ALEX)});
    CHECK(alex.value("personNames").toMap().value(person_text(PERSON_ALEX)).toString() == "Alex Sample");
    CHECK(alex.value("asksPerson").toBool());

    const QVariantMap forgotten = f.edit_of(5);
    CHECK(forgotten.value("personNames").toMap().value(person_text(PERSON_FORGOTTEN)).toString() == "someone");

    const QVariantMap gave = f.edit_of(3);
    CHECK(gave.value("value").toString() == "a toy");
    CHECK(gave.value("attachedNote").toString() == "was pleased");
    CHECK(gave.value("asksText").toBool());

    // The types it can be changed to: not the retired one.
    CHECK(f.model().interactionTypesFor(id_text(1)).size() == 3);
    CHECK(f.model().interactionTypesFor(id_text(4)).isEmpty()); // an error entry
}

TEST_CASE("edit.interaction: change the person, the answer, the note, the type and the time",
          "[viewmodel][edit][interaction]")
{
    Fixture f;

    SECTION("the people of a skipped entry are set, in the order picked")
    {
        QVariantMap edit;
        edit.insert("personIds", QStringList{person_text(PERSON_JORDAN), person_text(PERSON_ALEX)});
        REQUIRE(f.model().validateEdit(id_text(2), edit).isEmpty());
        CHECK(f.model().previewLine(id_text(2), edit) ==
              QStringLiteral("10:20 (Interaction) Spoke with Jordan Placeholder and Alex Sample"));
        REQUIRE(f.model().saveEntry(id_text(2), edit));
        f.commit();
        CHECK(f.line(1) == QStringLiteral("10:20 (Interaction) Spoke with Jordan Placeholder and Alex Sample"));
    }
    SECTION("the answer and the note")
    {
        QVariantMap edit;
        edit.insert("value", "a book");
        edit.insert("attachedNote", "");
        REQUIRE(f.model().saveEntry(id_text(3), edit));
        f.commit();
        CHECK(f.line(2) == QStringLiteral("10:25 (Interaction) Gave a book"));
    }
    SECTION("the type takes the name of the new one and the entry keeps its people")
    {
        QVariantMap edit;
        edit.insert("interactionTypeId", type_text(INTERACTION_GREETED));
        REQUIRE(f.model().saveEntry(id_text(1), edit));
        f.commit();
        CHECK(f.line(0) == QStringLiteral("10:10 (Interaction) Greeted"));
        const Entry* entry = f.m_Document->log().find(sample_id<EntryId>(1));
        REQUIRE(entry != nullptr);
        const auto& body = std::get<InteractionEntry>(entry->m_Body);
        CHECK(body.m_TypeLabel == "Greeted");
        CHECK(body.m_People.size() == 1);
    }
    SECTION("the time moves the row")
    {
        QVariantMap edit;
        edit.insert("localSeconds", 11 * 3600);
        REQUIRE(f.model().saveEntry(id_text(1), edit));
        f.commit();
        CHECK(f.line(5) == QStringLiteral("11:00 (Interaction) Spoke with Alex Sample"));
    }
    SECTION("a person can be taken off")
    {
        QVariantMap edit;
        edit.insert("personIds", QStringList{});
        REQUIRE(f.model().saveEntry(id_text(1), edit));
        f.commit();
        CHECK(f.line(0) == QStringLiteral("10:10 (Interaction) Spoke with (person not set)"));
    }
    SECTION("nothing changed is not a save")
    {
        QVariantMap edit;
        edit.insert("personIds", QStringList{person_text(PERSON_ALEX)});
        REQUIRE(f.model().saveEntry(id_text(1), edit));
        CHECK(f.m_Gateway.pending() == 0);
    }
}

TEST_CASE("edit.interaction: what is refused", "[viewmodel][edit][interaction]")
{
    Fixture f;
    QVariantMap edit;

    edit.insert("interactionTypeId", type_text(INTERACTION_VISITOR)); // retired: cannot be moved to
    CHECK_FALSE(f.model().validateEdit(id_text(1), edit).isEmpty());
    edit.clear();
    edit.insert("interactionTypeId", type_text(99));
    CHECK_FALSE(f.model().validateEdit(id_text(1), edit).isEmpty());
    edit.clear();
    edit.insert("interactionTypeId", "not an id");
    CHECK_FALSE(f.model().validateEdit(id_text(1), edit).isEmpty());

    edit.clear();
    QStringList many;
    for (std::uint64_t n = 1; n <= MAX_PEOPLE_PER_INTERACTION + 1; ++n)
    {
        many.append(person_text(100 + n));
    }
    edit.insert("personIds", many);
    CHECK_FALSE(f.model().validateEdit(id_text(1), edit).isEmpty());
    CHECK_FALSE(f.model().saveEntry(id_text(1), edit));

    edit.clear();
    edit.insert("personIds", QStringList{"nonsense"});
    CHECK_FALSE(f.model().validateEdit(id_text(1), edit).isEmpty());
}

TEST_CASE("review.interaction_rows: a past log shows interactions the same way and re-shares at the current setting",
          "[viewmodel][review][interaction][history]")
{
    TestEnvironment world;
    FixedPeople people;
    ExportPreferences preferences{QString()};
    world.m_People = &people;
    world.m_ExportPreferences = &preferences;
    EventLog events;
    MemoryStorage storage{world.m_Clock, &events};
    ManualStorageGateway gateway{storage.context()};
    FakeConfigurationSource config;
    FakeClipboard clipboard{&events};
    config.set(configuration_with_interactions());

    // A past log: archived, loaded as an archived document, shown by a second timeline (History's review model).
    const LogId id = sample_id<LogId>(1);
    REQUIRE(storage.memory().create_log(id, sample_header()));
    REQUIRE(storage.memory().add_entry(
        id, interaction_entry(1, on_day(10, 10), INTERACTION_SPOKE_WITH, {PERSON_ALEX, PERSON_JORDAN})));
    REQUIRE(storage.memory().add_entry(id, interaction_entry(2, on_day(10, 20), INTERACTION_SPOKE_WITH, {})));
    REQUIRE(storage.memory().archive_log(id));
    auto loaded = storage.memory().load_log(id);
    REQUIRE(loaded.has_value());
    auto document = std::make_shared<LogDocument>(world.environment(), gateway, std::move(*loaded));
    document->set_archived();
    TimelineModel model(world.environment(), config, clipboard);
    model.bind(document);

    // The screen: full names and the marker.
    REQUIRE(model.rowCount() == 2);
    CHECK(model.data(model.index(0), TimelineModel::FULL_LINE).toString() ==
          QStringLiteral("10:10 (Interaction) Spoke with Alex Sample and Jordan Placeholder"));
    CHECK(model.data(model.index(1), TimelineModel::FULL_LINE).toString() ==
          QStringLiteral("10:20 (Interaction) Spoke with (person not set)"));

    // Re-sharing: the setting in force now, not the one at the time the log was made.
    REQUIRE(model.copyAll());
    CHECK(clipboard.text().find("Spoke with A.S. and J.P.") != std::string::npos);
    CHECK(clipboard.text().find("Alex") == std::string::npos);

    preferences.set_name_export("full");
    REQUIRE(model.copyAll());
    CHECK(clipboard.text().find("Spoke with Alex Sample and Jordan Placeholder") != std::string::npos);

    preferences.set_name_export("hidden");
    REQUIRE(model.copyAll());
    CHECK(clipboard.text().find("Spoke with someone") != std::string::npos);
    CHECK(clipboard.text().find("Sample") == std::string::npos);
    CHECK(clipboard.text().find("Placeholder") == std::string::npos);
    REQUIRE(model.copyLine(0));
    CHECK(clipboard.text() == "10:10 (Interaction) Spoke with someone");
}

TEST_CASE("review.copy: a row that is not there, a row of an error and a model with no log copy what they can",
          "[viewmodel][review][interaction]")
{
    Fixture f;
    CHECK_FALSE(f.model().copyLine(-1));
    CHECK_FALSE(f.model().copyLine(100));
    REQUIRE(f.model().copyLine(3));
    CHECK(f.m_Clipboard.text().rfind("10:30", 0) == 0);

    TimelineModel unbound(f.m_World.environment(), f.m_Config, f.m_Clipboard);
    CHECK_FALSE(unbound.copyAll());
    CHECK_FALSE(unbound.copyLine(0));
}

TEST_CASE("edit.interaction: the types an entry can be changed to keep its own, retired or gone from the setup",
          "[viewmodel][edit][interaction]")
{
    Fixture f;
    CHECK(f.model().interactionTypesFor("not an id").isEmpty());
    CHECK(f.model().interactionTypesFor(id_text(99)).isEmpty());

    // Entry 6 is a Greeted. Retired since, it is still offered, once; the retired Visitor, not its own, is not.
    Configuration retired = configuration_with_interactions();
    for (InteractionType& type : retired.m_InteractionTypes)
    {
        type.m_Retired = type.m_Retired || type.m_Id == test_id<InteractionTypeId>(INTERACTION_GREETED);
    }
    f.m_Config.set(retired);
    CHECK(f.model().interactionTypesFor(id_text(6)).size() == 3);

    // Gone from the setup altogether: the entry's own type is added from the name it was logged with.
    Configuration gone = configuration_with_interactions();
    std::erase_if(gone.m_InteractionTypes, [](const InteractionType& type)
                  { return type.m_Id == test_id<InteractionTypeId>(INTERACTION_GREETED); });
    f.m_Config.set(gone);
    const QVariantList list = f.model().interactionTypesFor(id_text(6));
    REQUIRE(list.size() == 3);
    CHECK(list.back().toMap().value("typeId").toString() == type_text(INTERACTION_GREETED));
    const auto& body = std::get<InteractionEntry>(f.m_Document->log().find(sample_id<EntryId>(6))->m_Body);
    CHECK(list.back().toMap().value("name").toString() == QString::fromStdString(body.m_TypeLabel));

    // The same type, as it is, is allowed though the setup lost it; a type with no name is not.
    QVariantMap same;
    same.insert("interactionTypeId", type_text(INTERACTION_GREETED));
    CHECK(f.model().validateEdit(id_text(6), same).isEmpty());
    Configuration nameless = configuration_with_interactions();
    nameless.m_InteractionTypes.push_back(make_type(40, "", std::nullopt, InteractionAsks::NOTHING));
    f.m_Config.set(nameless);
    QVariantMap to_nameless;
    to_nameless.insert("interactionTypeId", type_text(40));
    CHECK_FALSE(f.model().validateEdit(id_text(1), to_nameless).isEmpty());
}

TEST_CASE("edit.interaction: what the sheet asks for follows the type, and what the entry already has",
          "[viewmodel][edit][interaction]")
{
    Fixture f;
    Configuration bare = configuration_with_interactions();
    bare.m_InteractionTypes.clear();
    f.m_Config.set(bare);

    // A type the setup does not know asks for nothing; people or an answer on the entry are still shown.
    const QVariantMap with_person = f.edit_of(1);
    CHECK(with_person.value("asksPerson").toBool());
    CHECK_FALSE(with_person.value("asksText").toBool());
    const QVariantMap plain = f.edit_of(2);
    CHECK_FALSE(plain.value("asksPerson").toBool());
    CHECK_FALSE(plain.value("asksText").toBool());
    const QVariantMap with_value = f.edit_of(3);
    CHECK_FALSE(with_value.value("asksPerson").toBool());
    CHECK(with_value.value("asksText").toBool());

    // With no list of people the names read as "someone".
    f.m_World.m_People = nullptr;
    TimelineModel nameless(f.m_World.environment(), f.m_Config, f.m_Clipboard);
    nameless.bind(f.m_Document);
    CHECK(nameless.entryForEdit(id_text(1)).value("personNames").toMap().value(person_text(PERSON_ALEX)).toString() ==
          "someone");
}

TEST_CASE("edit.interaction: an empty answer and an empty note are taken off, and a person picked twice stays once",
          "[viewmodel][edit][interaction]")
{
    Fixture f;

    SECTION("the answer and the note of entry 3 are cleared")
    {
        QVariantMap edit;
        edit.insert("value", "   ");
        edit.insert("attachedNote", "  ");
        REQUIRE(f.model().saveEntry(id_text(3), edit));
        f.commit();
        const auto& body = std::get<InteractionEntry>(f.m_Document->log().find(sample_id<EntryId>(3))->m_Body);
        CHECK_FALSE(body.m_Value.has_value());
        CHECK_FALSE(body.m_AttachedNote.has_value());
    }
    SECTION("a repeated person is one person")
    {
        QVariantMap edit;
        edit.insert("personIds",
                    QStringList{person_text(PERSON_ALEX), person_text(PERSON_ALEX), person_text(PERSON_JORDAN)});
        REQUIRE(f.model().saveEntry(id_text(2), edit));
        f.commit();
        const auto& body = std::get<InteractionEntry>(f.m_Document->log().find(sample_id<EntryId>(2))->m_Body);
        CHECK(body.m_People.size() == 2);
    }
}
