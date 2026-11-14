// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, package I-6: the model of the Interactions board, the interaction types in force, and what the session does
// with an interaction (log first, people and answer later). Names of people are the stand-ins of the docs.
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/rig.hpp"
#include "viewmodel/interaction_board_model.hpp"
#include "viewmodel/interaction_types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::InteractionBoardModel;
using Origin = viewmodel::LogChange::Origin;

namespace
{

constexpr int OTHER = 11;

struct InteractionFixture
{
    explicit InteractionFixture(Configuration configuration) : m_Board(m_Config, m_Open)
    {
        m_Config.set(std::move(configuration));
        m_Open.set(m_Rig.document.get());
    }

    DocumentRig m_Rig;
    FakeConfigurationSource m_Config;
    FakeOpenLogSource m_Open;
    InteractionBoardModel m_Board;
    std::uint64_t m_Next = 1;

    [[nodiscard]] QVariant value(int row, int role) const
    {
        return m_Board.data(m_Board.index(row), role);
    }

    EntryId add(const InteractionTypeId& type, const std::string& label)
    {
        Entry entry;
        entry.m_Id = sample_id<EntryId>(m_Next++);
        entry.m_Captured = Timestamp{sample_time(10), "Australia/Melbourne"};
        entry.m_Body = InteractionEntry{type, label, {}, std::nullopt, std::nullopt};
        const EntryId id = entry.m_Id;
        REQUIRE(m_Rig.document->add_entry(std::move(entry), Origin::USER, [](const Result<void, StorageError>&) {}));
        m_Rig.gateway().run_all();
        return id;
    }
};

Configuration configuration_without_interactions()
{
    Configuration configuration = sample_configuration();
    configuration.m_InteractionTypes.clear();
    return configuration;
}

} // namespace

TEST_CASE("With no interaction types in the setup the built-in ones are in force", "[viewmodel][interaction][unit]")
{
    const auto types = viewmodel::interaction_types_in_force(configuration_without_interactions());
    REQUIRE(types.size() == 6);
    CHECK(types[0].m_Name == "Spoke with");
    CHECK(types[0].m_Asks == InteractionAsks::PERSON);
    CHECK(types[1].m_Asks == InteractionAsks::TEXT);
    CHECK(types[2].m_Asks == InteractionAsks::NOTHING);
    for (std::size_t i = 0; i < types.size(); ++i)
    {
        CHECK(types[i].m_BoardPosition == static_cast<int>(i) + 1);
    }

    Configuration own = sample_configuration();
    own.m_InteractionTypes.resize(1);
    CHECK(viewmodel::interaction_types_in_force(own).size() == 1);
}

TEST_CASE("The Interactions board has twelve tiles, the types on their positions and Other last",
          "[viewmodel][interaction][unit]")
{
    InteractionFixture f(configuration_without_interactions());
    REQUIRE(f.m_Board.rowCount() == 12);
    CHECK_FALSE(f.m_Board.board_empty());
    CHECK(f.value(0, InteractionBoardModel::LABEL).toString() == "Spoke with");
    CHECK(f.value(5, InteractionBoardModel::LABEL).toString() == "Helped");
    CHECK(f.value(6, InteractionBoardModel::IS_EMPTY).toBool());
    CHECK(f.value(OTHER, InteractionBoardModel::IS_OTHER).toBool());
    CHECK(f.value(0, InteractionBoardModel::COLOUR_SLOT).toInt() == 1);
}

TEST_CASE("Counts follow the log, and a type that is not on the board counts in Other",
          "[viewmodel][interaction][unit]")
{
    InteractionFixture f(configuration_without_interactions());
    const auto types = viewmodel::interaction_types_in_force(configuration_without_interactions());

    QSignalSpy data(&f.m_Board, &QAbstractItemModel::dataChanged);
    const EntryId first = f.add(types[0].m_Id, types[0].m_Name);
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 1);
    CHECK(f.value(0, InteractionBoardModel::HAS_COUNT).toBool());
    CHECK(f.value(0, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Spoke with, 1 today");
    CHECK(data.count() == 1);

    f.add(InteractionTypeId(Uuid::from_random(1, 2)), "Retired type");
    CHECK(f.value(OTHER, InteractionBoardModel::COUNT).toInt() == 1);

    REQUIRE(f.m_Rig.document->set_deleted(first, true, Origin::UNDO, [](const Result<void, StorageError>&) {}));
    f.m_Rig.gateway().run_all();
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 0);
}

TEST_CASE("The board says what each type asks for", "[viewmodel][interaction][unit]")
{
    InteractionFixture f(configuration_without_interactions());
    const auto types = viewmodel::interaction_types_in_force(configuration_without_interactions());
    const auto id = [&types](std::size_t i) { return QString::fromStdString(types[i].m_Id.format()); };
    CHECK(f.m_Board.asksFor(id(0)) == "person");
    CHECK(f.m_Board.asksFor(id(1)) == "text");
    CHECK(f.m_Board.asksFor(id(2)) == "nothing");
    CHECK(f.m_Board.hasValueSlot(id(3)));
    CHECK_FALSE(f.m_Board.hasValueSlot(id(0)));
    CHECK(f.m_Board.previewLine(id(1), "Morning Medley") == "Played Morning Medley");
}

TEST_CASE("An interaction is logged with no people, then people and an answer are put on it",
          "[viewmodel][interaction][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    auto& session = rig.session();
    const auto types = viewmodel::interaction_types_in_force(rig.settings().configuration());
    const QString spoke = QString::fromStdString(types[0].m_Id.format());

    const QString entry = session.recordInteractionEntry(spoke);
    REQUIRE_FALSE(entry.isEmpty());
    rig.settle();
    CHECK(rig.root()
              .interaction_board()
              ->data(rig.root().interaction_board()->index(0), InteractionBoardModel::COUNT)
              .toInt() == 1);

    const QString person_a = QString::fromStdString(Uuid::from_random(11, 12).format());
    const QString person_b = QString::fromStdString(Uuid::from_random(13, 14).format());
    CHECK(session.setInteractionPeople(entry, {person_a, person_b}));
    rig.settle();
    const auto log = rig.stored_open_log();
    REQUIRE(log.has_value());
    const Entry* stored = log->m_Log.find(EntryId(*Uuid::parse(entry.toStdString())));
    REQUIRE(stored != nullptr);
    const auto& body = std::get<InteractionEntry>(stored->m_Body);
    REQUIRE(body.m_People.size() == 2);
    CHECK(body.m_People[0].format() == person_a.toStdString());
    CHECK(body.m_TypeLabel == "Spoke with");
    CHECK_FALSE(body.m_Value.has_value());

    // An answer, a draft as the note, and a refusal of what is not an interaction entry.
    CHECK(session.setInteractionValue(entry, "  Morning Medley  "));
    rig.settle();
    session.set_draft("Asked twice");
    CHECK(session.attachDraft(entry));
    rig.settle();
    const auto again = rig.stored_open_log();
    const auto& changed =
        std::get<InteractionEntry>(again->m_Log.find(EntryId(*Uuid::parse(entry.toStdString())))->m_Body);
    CHECK(changed.m_Value == std::optional<std::string>("Morning Medley"));
    CHECK(changed.m_AttachedNote == std::optional<std::string>("Asked twice"));
    CHECK_FALSE(session.setInteractionValue(entry, "   "));
    CHECK_FALSE(session.setInteractionPeople("not an id", {}));
}

TEST_CASE("An interaction that is unknown or retired is not logged", "[viewmodel][interaction][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    CHECK(rig.session().recordInteractionEntry(QString::fromStdString(Uuid::from_random(5, 6).format())).isEmpty());
    CHECK(rig.session().recordInteractionEntry("not an id").isEmpty());
}

TEST_CASE("shell.empty_board: a setup whose interaction types are all retired leaves the board empty",
          "[viewmodel][interaction][unit]")
{
    Configuration configuration = sample_configuration();
    for (InteractionType& type : configuration.m_InteractionTypes)
    {
        type.m_Retired = true;
    }
    InteractionFixture f(std::move(configuration));
    CHECK(f.m_Board.board_empty());
    CHECK(f.value(0, InteractionBoardModel::IS_EMPTY).toBool());
}
