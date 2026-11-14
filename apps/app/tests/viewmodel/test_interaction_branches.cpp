// SPDX-License-Identifier: GPL-3.0-or-later
// Stage 14, packages I-6 and I-7: the refusals, the quiet paths and the other side of the choices in the model of the
// Interactions board, the miniature layout, what the session does with an interaction, and the document's edit by a
// function. Names of people are the stand-ins of the docs.
#include "golden_scenarios.hpp"
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/recording_repository.hpp"
#include "support/rig.hpp"
#include "viewmodel/board_layout_model.hpp"
#include "viewmodel/interaction_board_model.hpp"
#include "viewmodel/interaction_types.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::BoardLayoutModel;
using viewmodel::InteractionBoardModel;
using viewmodel::LogDocument;
using Origin = viewmodel::LogChange::Origin;

namespace
{

constexpr int OTHER = 11;
constexpr std::uint64_t T_SPOKE = 21;
constexpr std::uint64_t T_GAVE = 22;
constexpr std::uint64_t T_GREETED = 23;
constexpr std::uint64_t T_RETIRED = 24;
constexpr std::uint64_t T_OFF_BOARD = 25;
constexpr std::uint64_t T_POSITION_ZERO = 26;
constexpr std::uint64_t T_POSITION_TOO_HIGH = 27;
constexpr std::uint64_t T_CLAIMS_TAKEN_TILE = 28;
constexpr std::uint64_t T_UNKNOWN = 99;

InteractionType make_type(std::uint64_t number, std::string name, std::optional<std::string> pattern,
                          InteractionAsks asks, std::optional<int> position, bool retired = false)
{
    InteractionType type;
    type.m_Id = test_id<InteractionTypeId>(number);
    type.m_Name = std::move(name);
    type.m_LinePattern = std::move(pattern);
    type.m_Asks = asks;
    type.m_BoardPosition = position;
    type.m_Retired = retired;
    return type;
}

QString type_text(std::uint64_t number)
{
    return QString::fromStdString(test_id<InteractionTypeId>(number).format());
}

Configuration configuration_with_odd_types()
{
    Configuration configuration = sample_configuration();
    configuration.m_InteractionTypes = {
        make_type(T_SPOKE, "Spoke with", "Spoke with {person}", InteractionAsks::PERSON, 1),
        make_type(T_GAVE, "Gave", "Gave {value}", InteractionAsks::TEXT, 2),
        make_type(T_GREETED, "Greeted", std::nullopt, InteractionAsks::NOTHING, 3),
        make_type(T_RETIRED, "Visitor", std::nullopt, InteractionAsks::NOTHING, 4, true),
        make_type(T_OFF_BOARD, "Waved", std::nullopt, InteractionAsks::NOTHING, std::nullopt),
        make_type(T_POSITION_ZERO, "Zero", std::nullopt, InteractionAsks::NOTHING, 0),
        make_type(T_POSITION_TOO_HIGH, "Too high", std::nullopt, InteractionAsks::NOTHING, 12),
        make_type(T_CLAIMS_TAKEN_TILE, "Claims one", std::nullopt, InteractionAsks::NOTHING, 1)};
    return configuration;
}

QVariantList other_types_of(const InteractionBoardModel& board)
{
    return board.other_types();
}

struct BoardFixture
{
    explicit BoardFixture(Configuration configuration) : m_Board(m_Config, m_Open)
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

    void commit(Entry entry)
    {
        REQUIRE(m_Rig.document->add_entry(std::move(entry), Origin::USER, [](const Result<void, StorageError>&) {}));
        m_Rig.gateway().run_all();
    }

    EntryId add_interaction(std::uint64_t type)
    {
        Entry entry;
        entry.m_Id = sample_id<EntryId>(m_Next++);
        entry.m_Captured = Timestamp{sample_time(10), "Australia/Melbourne"};
        entry.m_Body = InteractionEntry{test_id<InteractionTypeId>(type), "label", {}, std::nullopt, std::nullopt};
        const EntryId id = entry.m_Id;
        commit(std::move(entry));
        return id;
    }

    void set_deleted(const EntryId& id, bool deleted)
    {
        REQUIRE(m_Rig.document->set_deleted(id, deleted, Origin::USER, [](const Result<void, StorageError>&) {}));
        m_Rig.gateway().run_all();
    }
};

QString first_entry_of_kind_is_note(Rig& rig)
{
    const auto log = rig.stored_open_log();
    REQUIRE(log.has_value());
    for (const Entry& entry : log->m_Log.entries())
    {
        if (std::holds_alternative<NoteEntry>(entry.m_Body))
        {
            return QString::fromStdString(entry.m_Id.format());
        }
    }
    FAIL("no note entry");
    return {};
}

} // namespace

// ------------------------------------------------------------------------------------------------ the board model

TEST_CASE("board.rows: a tile with a type, an empty tile and Other each answer every role",
          "[viewmodel][interaction][unit]")
{
    BoardFixture f(configuration_with_odd_types());

    CHECK(f.m_Board.rowCount() == 12);
    CHECK(f.m_Board.rowCount(f.m_Board.index(0)) == 0);
    CHECK(f.m_Board.roleNames().size() == 8);

    CHECK(f.value(0, InteractionBoardModel::TYPE_ID).toString() == type_text(T_SPOKE));
    CHECK(f.value(0, InteractionBoardModel::LABEL).toString() == "Spoke with");
    CHECK(f.value(0, Qt::DisplayRole).toString() == "Spoke with");
    CHECK(f.value(0, InteractionBoardModel::COLOUR_SLOT).toInt() >= 1);
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 0);
    CHECK_FALSE(f.value(0, InteractionBoardModel::HAS_COUNT).toBool());
    CHECK_FALSE(f.value(0, InteractionBoardModel::IS_OTHER).toBool());
    CHECK_FALSE(f.value(0, InteractionBoardModel::IS_EMPTY).toBool());
    CHECK(f.value(0, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Spoke with");

    // The retired type and the ones with no usable position are on no tile: tile 4 and tile 5 are empty.
    CHECK(f.value(3, InteractionBoardModel::IS_EMPTY).toBool());
    CHECK(f.value(3, InteractionBoardModel::LABEL).toString().isEmpty());
    CHECK(f.value(3, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Position 4, empty. Add interaction type");

    CHECK(f.value(OTHER, InteractionBoardModel::IS_OTHER).toBool());
    CHECK_FALSE(f.value(OTHER, InteractionBoardModel::IS_EMPTY).toBool());
    CHECK(f.value(OTHER, InteractionBoardModel::LABEL).toString() == QString::fromUtf8("Other\xE2\x80\xA6"));
    CHECK(f.value(OTHER, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Other");

    // What is not a row, and what is not a role.
    CHECK_FALSE(f.m_Board.data(QModelIndex(), InteractionBoardModel::LABEL).isValid());
    CHECK_FALSE(f.m_Board.data(f.m_Board.index(40), InteractionBoardModel::LABEL).isValid());
    CHECK_FALSE(f.value(0, InteractionBoardModel::LABEL + 100).isValid());
}

TEST_CASE(
    "board.counts: today's counts follow the log, Other takes what no tile shows, deleted and errors do not count",
    "[viewmodel][interaction][unit]")
{
    BoardFixture f(configuration_with_odd_types());
    const EntryId first = f.add_interaction(T_SPOKE);
    f.add_interaction(T_SPOKE);
    f.add_interaction(T_GAVE);
    f.add_interaction(T_UNKNOWN);
    f.add_interaction(T_RETIRED);
    f.add_interaction(T_OFF_BOARD);
    f.commit(sample_error(90, 12));

    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 2);
    CHECK(f.value(0, InteractionBoardModel::HAS_COUNT).toBool());
    CHECK(f.value(0, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Spoke with, 2 today");
    CHECK(f.value(1, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Gave, 1 today");
    CHECK(f.value(2, InteractionBoardModel::COUNT).toInt() == 0);
    // The unknown, the retired and the off-board types all count in Other.
    CHECK(f.value(OTHER, InteractionBoardModel::COUNT).toInt() == 3);
    CHECK(f.value(OTHER, InteractionBoardModel::ACCESSIBLE_NAME).toString() == "Other, 3 today");

    QSignalSpy changed(&f.m_Board, &QAbstractItemModel::dataChanged);
    f.set_deleted(first, true);
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 1);
    CHECK(changed.count() >= 1);
    f.set_deleted(first, false);
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 2);
}

TEST_CASE("board.counts: an event that does not change a count tells nobody, and a log that goes leaves no counts",
          "[viewmodel][interaction][unit]")
{
    BoardFixture f(configuration_with_odd_types());
    f.add_interaction(T_SPOKE);
    QSignalSpy changed(&f.m_Board, &QAbstractItemModel::dataChanged);

    // A note is an entry the board does not count; a share is not an entry at all.
    f.commit(sample_note(80, 11, "A note"));
    CHECK(changed.count() == 0);
    REQUIRE(f.m_Rig.document->mark_shared(f.m_Rig.world().m_Clock.now(), [](const Result<void, StorageError>&) {}));
    f.m_Rig.gateway().run_all();
    CHECK(changed.count() == 0);
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 1);

    // With no open log the board is built from the setup alone.
    f.m_Open.set(nullptr);
    CHECK(f.value(0, InteractionBoardModel::COUNT).toInt() == 0);
    CHECK(f.value(0, InteractionBoardModel::LABEL).toString() == "Spoke with");
}

TEST_CASE("board.other_types: the live types that are on no tile are listed for the Other sheet",
          "[viewmodel][interaction][unit]")
{
    BoardFixture f(configuration_with_odd_types());
    const QVariantList other = f.m_Board.other_types();

    // Off the board, position 0, position 12 and the second claim on tile 1; not the retired one.
    REQUIRE(other.size() == 4);
    CHECK(other.at(0).toMap().value("typeId").toString() == type_text(T_OFF_BOARD));
    CHECK(other.at(0).toMap().value("label").toString() == "Waved");
    CHECK(other.at(3).toMap().value("typeId").toString() == type_text(T_CLAIMS_TAKEN_TILE));
    CHECK_FALSE(f.m_Board.board_empty());
}

TEST_CASE("board.asks: what a type asks for, whether its line has a place for an answer, and its name",
          "[viewmodel][interaction][unit]")
{
    BoardFixture f(configuration_with_odd_types());

    CHECK(f.m_Board.asksFor(type_text(T_SPOKE)) == "person");
    CHECK(f.m_Board.asksFor(type_text(T_GAVE)) == "text");
    CHECK(f.m_Board.asksFor(type_text(T_GREETED)) == "nothing");
    CHECK(f.m_Board.asksFor(type_text(T_UNKNOWN)) == "nothing");
    CHECK(f.m_Board.asksFor("not an id") == "nothing");

    CHECK(f.m_Board.hasValueSlot(type_text(T_GAVE)));
    CHECK_FALSE(f.m_Board.hasValueSlot(type_text(T_SPOKE)));
    CHECK_FALSE(f.m_Board.hasValueSlot(type_text(T_GREETED))); // no line at all
    CHECK_FALSE(f.m_Board.hasValueSlot(type_text(T_UNKNOWN)));

    CHECK(f.m_Board.typeName(type_text(T_GAVE)) == "Gave");
    CHECK(f.m_Board.typeName(type_text(T_UNKNOWN)).isEmpty());

    CHECK(f.m_Board.previewLine(type_text(T_GAVE), "a toy") == "Gave a toy");
    CHECK_FALSE(f.m_Board.previewLine(type_text(T_GAVE), "   ").isEmpty());
    CHECK(f.m_Board.previewLine(type_text(T_GREETED), "ignored") == "Greeted");
    CHECK(f.m_Board.previewLine(type_text(T_UNKNOWN), "a toy").isEmpty());
}

TEST_CASE("board.setup: a change of setup is told to the tiles, and to the board only when it empties or fills",
          "[viewmodel][interaction][unit]")
{
    BoardFixture f(configuration_with_odd_types());
    QSignalSpy empty_changed(&f.m_Board, &InteractionBoardModel::boardEmptyChanged);
    QSignalSpy other_changed(&f.m_Board, &InteractionBoardModel::otherTypesChanged);
    QSignalSpy data_changed(&f.m_Board, &QAbstractItemModel::dataChanged);

    // The same setup again: the tiles are told (they are read again), the two properties are not.
    f.m_Config.set(configuration_with_odd_types());
    CHECK(data_changed.count() == 1);
    CHECK(empty_changed.count() == 0);
    CHECK(other_changed.count() == 0);

    // A type that is on no tile is a change of the Other sheet only.
    Configuration more = configuration_with_odd_types();
    more.m_InteractionTypes.push_back(make_type(30, "Extra", std::nullopt, InteractionAsks::NOTHING, std::nullopt));
    f.m_Config.set(more);
    CHECK(empty_changed.count() == 0);
    CHECK(other_changed.count() == 1);
    CHECK(other_types_of(f.m_Board).size() == 5);

    // All retired: the board is empty, and the Other sheet has nothing.
    Configuration none = configuration_with_odd_types();
    for (InteractionType& type : none.m_InteractionTypes)
    {
        type.m_Retired = true;
    }
    f.m_Config.set(none);
    CHECK(empty_changed.count() == 1);
    CHECK(f.m_Board.board_empty());
    CHECK(f.m_Board.other_types().isEmpty());
    CHECK(other_changed.count() == 2);

    // And back.
    f.m_Config.set(configuration_with_odd_types());
    CHECK(empty_changed.count() == 2);
}

// ------------------------------------------------------------------------------------------------ the miniature

TEST_CASE("settings.mini_board: the Interactions miniature shows live types on their tiles, and says what an empty "
          "tile is for",
          "[viewmodel][interaction][settings][unit]")
{
    BoardLayoutModel model(nullptr, BoardLayoutModel::Board::INTERACTIONS);
    Configuration configuration = configuration_with_odd_types();
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    model.refresh(configuration);
    CHECK(changed.count() == 1);

    CHECK(model.rowCount() == 12);
    CHECK(model.rowCount(model.index(0)) == 0);
    CHECK(model.roleNames().size() == 7);
    CHECK(model.data(model.index(0), BoardLayoutModel::TYPE_ID).toString() == type_text(T_SPOKE));
    CHECK(model.data(model.index(0), BoardLayoutModel::LABEL).toString() == "Spoke with");
    CHECK(model.data(model.index(0), Qt::DisplayRole).toString() == "Spoke with");
    CHECK(model.data(model.index(0), BoardLayoutModel::POSITION).toInt() == 1);
    CHECK(model.data(model.index(0), BoardLayoutModel::ACCESSIBLE_NAME).toString() == "Spoke with");
    CHECK_FALSE(model.data(model.index(0), BoardLayoutModel::IS_EMPTY).toBool());
    CHECK_FALSE(model.data(model.index(0), BoardLayoutModel::IS_OTHER).toBool());
    CHECK(model.data(model.index(0), BoardLayoutModel::COLOUR_SLOT).toInt() >= 1);

    CHECK(model.data(model.index(5), BoardLayoutModel::IS_EMPTY).toBool());
    CHECK(model.data(model.index(5), BoardLayoutModel::ACCESSIBLE_NAME).toString() ==
          "Position 6, empty. Add interaction type");
    CHECK(model.data(model.index(OTHER), BoardLayoutModel::IS_OTHER).toBool());
    CHECK(model.data(model.index(OTHER), BoardLayoutModel::ACCESSIBLE_NAME).toString() == "Other");

    CHECK_FALSE(model.data(QModelIndex(), BoardLayoutModel::LABEL).isValid());
    CHECK_FALSE(model.data(model.index(40), BoardLayoutModel::LABEL).isValid());
    CHECK_FALSE(model.data(model.index(0), BoardLayoutModel::LABEL + 100).isValid());

    // The same setup again changes no tile; a move changes the range between the two.
    model.refresh(configuration);
    CHECK(changed.count() == 1);
    configuration.m_InteractionTypes[0].m_BoardPosition = 5;
    model.refresh(configuration);
    REQUIRE(changed.count() == 2);
    CHECK(changed.at(1).at(0).value<QModelIndex>().row() == 0);
    CHECK(changed.at(1).at(1).value<QModelIndex>().row() == 4);
}

TEST_CASE("settings.mini_board: the error miniature names an empty tile as one for an error type, and leaves retired "
          "ones off",
          "[viewmodel][settings][unit]")
{
    BoardLayoutModel model;
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[1].m_Retired = true;
    model.refresh(configuration);

    CHECK(model.data(model.index(1), BoardLayoutModel::IS_EMPTY).toBool());
    CHECK(model.data(model.index(1), BoardLayoutModel::ACCESSIBLE_NAME).toString() ==
          "Position 2, empty. Add error type");
    CHECK(model.data(model.index(0), BoardLayoutModel::ACCESSIBLE_NAME).toString() ==
          model.data(model.index(0), BoardLayoutModel::LABEL).toString());
    CHECK(model.data(model.index(0), BoardLayoutModel::POSITION).toInt() == 1);
}

// ---------------------------------------------------------------------------------------------- the session

TEST_CASE("interaction.record: a retired type, and an add the store refuses, log nothing and say so",
          "[viewmodel][interaction][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    rig.settle();
    auto& session = rig.session();
    const auto types = viewmodel::interaction_types_in_force(rig.settings().configuration());
    const QString first = QString::fromStdString(types[0].m_Id.format());
    const QString second = QString::fromStdString(types[1].m_Id.format());

    // The store refuses the add: the id is given, the entry is not kept, and the failure is told.
    QSignalSpy failed(&session, &viewmodel::LogSession::recordFailed);
    rig.repository().fail_next(RepositoryOp::ADD_ENTRY);
    const QString lost = session.recordInteractionEntry(first);
    rig.settle();
    CHECK_FALSE(lost.isEmpty());
    CHECK(failed.count() == 1);
    {
        const auto log = rig.stored_open_log();
        REQUIRE(log.has_value());
        CHECK(log->m_Log.entries().empty());
    }

    // A type that was retired after the board was drawn is not logged.
    REQUIRE(rig.settings().retireInteractionType(second).isEmpty());
    rig.settle();
    CHECK(session.recordInteractionEntry(second).isEmpty());
    CHECK(failed.count() == 1);
}

TEST_CASE("interaction.people: what is refused, and that a person picked twice is on the entry once",
          "[viewmodel][interaction][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    rig.settle();
    auto& session = rig.session();
    const auto types = viewmodel::interaction_types_in_force(rig.settings().configuration());
    const QString entry = session.recordInteractionEntry(QString::fromStdString(types[0].m_Id.format()));
    REQUIRE_FALSE(entry.isEmpty());
    rig.settle();
    const QString person = QString::fromStdString(Uuid::from_random(11, 12).format());
    const QString other = QString::fromStdString(Uuid::from_random(13, 14).format());
    const QString unknown_entry = QString::fromStdString(Uuid::from_random(15, 16).format());

    CHECK_FALSE(session.setInteractionPeople(unknown_entry, {person}));
    CHECK_FALSE(session.setInteractionPeople(entry, {person, "not an id"}));
    QStringList too_many;
    for (std::uint64_t n = 0; n <= MAX_PEOPLE_PER_INTERACTION; ++n)
    {
        too_many.append(QString::fromStdString(Uuid::from_random(100 + n, 7).format()));
    }
    CHECK_FALSE(session.setInteractionPeople(entry, too_many));

    // A repeat is the same person.
    CHECK(session.setInteractionPeople(entry, {person, other, person}));
    rig.settle();
    const auto log = rig.stored_open_log();
    REQUIRE(log.has_value());
    const auto& body = std::get<InteractionEntry>(log->m_Log.find(EntryId(*Uuid::parse(entry.toStdString())))->m_Body);
    CHECK(body.m_People.size() == 2);

    // Nobody: the entry is left with no people.
    CHECK(session.setInteractionPeople(entry, {}));
    rig.settle();
    const auto cleared = rig.stored_open_log();
    CHECK(std::get<InteractionEntry>(cleared->m_Log.find(EntryId(*Uuid::parse(entry.toStdString())))->m_Body)
              .m_People.empty());

    // An entry that is not an interaction takes no people and no answer.
    REQUIRE(session.addNote("A note about the day"));
    rig.settle();
    const QString note = first_entry_of_kind_is_note(rig);
    CHECK_FALSE(session.setInteractionPeople(note, {person}));
    CHECK_FALSE(session.setInteractionValue(note, "a toy"));
    session.set_draft("Some words");
    CHECK_FALSE(session.attachDraft(note));

    // An interaction that was deleted takes nothing either.
    REQUIRE(rig.timeline().deleteEntry(entry));
    rig.settle();
    CHECK_FALSE(session.setInteractionPeople(entry, {person}));
    CHECK_FALSE(session.setInteractionValue(entry, "a toy"));
}

TEST_CASE("interaction.value: the answer is trimmed, at most a thousand characters, and not empty",
          "[viewmodel][interaction][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    rig.settle();
    auto& session = rig.session();
    const auto types = viewmodel::interaction_types_in_force(rig.settings().configuration());
    const QString entry = session.recordInteractionEntry(QString::fromStdString(types[0].m_Id.format()));
    REQUIRE_FALSE(entry.isEmpty());
    rig.settle();
    const QString unknown_entry = QString::fromStdString(Uuid::from_random(15, 16).format());

    CHECK_FALSE(session.setInteractionValue("not an id", "a toy"));
    CHECK_FALSE(session.setInteractionValue(unknown_entry, "a toy"));
    CHECK_FALSE(session.setInteractionValue(entry, ""));
    CHECK_FALSE(session.setInteractionValue(entry, QString(1001, 'x')));
    CHECK(session.setInteractionValue(entry, QString(1000, 'x')));
    rig.settle();
    const auto log = rig.stored_open_log();
    REQUIRE(log.has_value());
    const auto& body = std::get<InteractionEntry>(log->m_Log.find(EntryId(*Uuid::parse(entry.toStdString())))->m_Body);
    CHECK(body.m_Value.has_value());
    CHECK(body.m_Value->size() == 1000);
}

TEST_CASE("interaction.note: a draft goes on an interaction once, and not on an entry that is not there or has none",
          "[viewmodel][interaction][unit]")
{
    Rig rig(RigStore::MEMORY);
    rig.import_sample_configuration();
    rig.settle();
    auto& session = rig.session();
    const auto types = viewmodel::interaction_types_in_force(rig.settings().configuration());
    const QString entry = session.recordInteractionEntry(QString::fromStdString(types[0].m_Id.format()));
    REQUIRE_FALSE(entry.isEmpty());
    rig.settle();

    // No draft, no entry.
    session.set_draft("");
    CHECK_FALSE(session.attachDraft(entry));
    session.set_draft("Asked twice");
    CHECK_FALSE(session.attachDraft("not an id"));
    CHECK_FALSE(session.attachDraft(QString::fromStdString(Uuid::from_random(15, 16).format())));

    CHECK(session.attachDraft(entry));
    rig.settle();
    // It has a note now: another draft is refused, and the draft stays.
    session.set_draft("Asked a third time");
    CHECK_FALSE(session.attachDraft(entry));
}

// ----------------------------------------------------------------------------------------------- the document

TEST_CASE("edit.mutator_refusal: a change that makes an invalid entry, or names an entry that is not there, is refused",
          "[viewmodel][document][edit][unit]")
{
    DocumentRig rig;
    std::vector<Result<void, StorageError>> results;
    REQUIRE(rig.document->add_entry(sample_interaction_entry(1, 10, {}), Origin::USER, rig.keep(results)));
    rig.gateway().run_all();
    results.clear();

    // Too many people for one interaction: the entry does not validate, and nothing is written.
    const auto crowd = [](Entry& entry) -> std::optional<std::string>
    {
        auto& body = std::get<InteractionEntry>(entry.m_Body);
        for (std::uint64_t n = 1; n <= MAX_PEOPLE_PER_INTERACTION + 1; ++n)
        {
            body.m_People.push_back(sample_id<PersonId>(n));
        }
        return std::nullopt;
    };
    const auto refused =
        rig.document->edit_entry(sample_id<EntryId>(1), LogDocument::Mutator(crowd), rig.keep(results));
    CHECK_FALSE(refused.has_value());
    rig.gateway().run_all();
    CHECK(std::get<InteractionEntry>(rig.document->log().find(sample_id<EntryId>(1))->m_Body).m_People.empty());

    // An entry that is not in the log.
    const auto nothing = [](Entry&) -> std::optional<std::string> { return std::nullopt; };
    const auto unknown =
        rig.document->edit_entry(sample_id<EntryId>(9), LogDocument::Mutator(nothing), rig.keep(results));
    REQUIRE_FALSE(unknown.has_value());
    CHECK(unknown.error().m_Log == Log::Error::UNKNOWN_ENTRY);
    rig.gateway().run_all();

    // A refusal that names a reason is told at once, and an edit that is fine behind it is written.
    const auto refuse = [](Entry&) -> std::optional<std::string> { return "Not now."; };
    const auto fine = [](Entry& entry) -> std::optional<std::string>
    {
        std::get<InteractionEntry>(entry.m_Body).m_Value = "a toy";
        return std::nullopt;
    };
    CHECK_FALSE(
        rig.document->edit_entry(sample_id<EntryId>(1), LogDocument::Mutator(refuse), rig.keep(results)).has_value());
    REQUIRE(rig.document->edit_entry(sample_id<EntryId>(1), LogDocument::Mutator(fine), rig.keep(results)));
    rig.gateway().run_all();
    REQUIRE(results.size() == 1);
    CHECK(results[0].has_value());
    CHECK(std::get<InteractionEntry>(rig.document->log().find(sample_id<EntryId>(1))->m_Body).m_Value ==
          std::optional<std::string>("a toy"));
}
