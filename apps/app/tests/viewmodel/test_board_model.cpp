// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/board_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QList>
#include <QModelIndex>
#include <QObject>
#include <QSignalSpy>
#include <QString>
#include <QVariant>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::BoardModel;
using viewmodel::LogChange;
using Origin = LogChange::Origin;

namespace
{

constexpr int OTHER = 11;

/// An open log, the sample configuration and a board over them. The tap helper stands in for the QML press: the
/// impact haptic on touch-down, then the entry, then the confirmation when its commit has finished.
struct BoardFixture
{
    explicit BoardFixture(Configuration configuration = sample_configuration()) : m_Board(m_Config, m_Open)
    {
        m_Config.set(std::move(configuration));
        m_Open.set(m_Rig.document.get());
    }

    BoardFixture(const BoardFixture&) = delete;
    BoardFixture& operator=(const BoardFixture&) = delete;

    DocumentRig m_Rig;
    FakeConfigurationSource m_Config;
    FakeOpenLogSource m_Open;
    FakeFeedbackSink m_Feedback{&m_Rig.events()};
    BoardModel m_Board;
    std::uint64_t m_NextEntry = 1;
    std::vector<EntryId> m_Entries;

    [[nodiscard]] const Configuration& configuration() const
    {
        return m_Config.configuration();
    }
    [[nodiscard]] ErrorTypeId type(std::size_t master_index) const
    {
        return configuration().m_ErrorTypes.at(master_index).m_Id;
    }

    [[nodiscard]] Entry error_entry(ErrorTypeId type_id, std::int64_t minute)
    {
        std::string label = "unknown";
        for (const ErrorType& type : configuration().m_ErrorTypes)
        {
            if (type.m_Id == type_id)
            {
                label = type.m_Name;
            }
        }
        Entry entry;
        entry.m_Id = sample_id<EntryId>(m_NextEntry++);
        entry.m_Captured = Timestamp{sample_time(minute), "Australia/Melbourne"};
        entry.m_Body = ErrorEntry{type_id, std::nullopt, std::move(label)};
        return entry;
    }

    /// The press: touch-down first, then the command; the confirmation follows the commit.
    EntryId tap(ErrorTypeId type_id, std::int64_t minute = 10)
    {
        m_Feedback.acknowledge_touch();
        Entry entry = error_entry(type_id, minute);
        const EntryId id = entry.m_Id;
        REQUIRE(m_Rig.document->add_entry(std::move(entry), Origin::USER,
                                          [this, id](const Result<void, StorageError>& result)
                                          {
                                              if (result)
                                              {
                                                  m_Feedback.entry_recorded(id);
                                              }
                                          }));
        m_Entries.push_back(id);
        return id;
    }

    /// A tap that has committed.
    EntryId tap_and_commit(ErrorTypeId type_id, std::int64_t minute = 10)
    {
        const EntryId id = tap(type_id, minute);
        m_Rig.gateway().run_all();
        return id;
    }

    void undo(EntryId id)
    {
        REQUIRE(m_Rig.document->set_deleted(id, true, Origin::UNDO, m_Rig.keep(m_Results)));
        m_Rig.gateway().run_all();
    }
    void restore(EntryId id)
    {
        REQUIRE(m_Rig.document->set_deleted(id, false, Origin::USER, m_Rig.keep(m_Results)));
        m_Rig.gateway().run_all();
    }

    [[nodiscard]] QVariant value(int row, int role) const
    {
        return m_Board.data(m_Board.index(row), role);
    }
    [[nodiscard]] int count(int row) const
    {
        return value(row, BoardModel::COUNT).toInt();
    }
    [[nodiscard]] QString type_text(ErrorTypeId id) const
    {
        return QString::fromStdString(id.format());
    }

    std::vector<Result<void, StorageError>> m_Results;
};

/// Counts the signals a board must never send or must send exactly once.
struct Spies
{
    explicit Spies(BoardModel& board)
        : m_Data(&board, &QAbstractItemModel::dataChanged), m_Reset(&board, &QAbstractItemModel::modelReset),
          m_AboutToReset(&board, &QAbstractItemModel::modelAboutToBeReset),
          m_Layout(&board, &QAbstractItemModel::layoutChanged), m_Inserted(&board, &QAbstractItemModel::rowsInserted),
          m_Removed(&board, &QAbstractItemModel::rowsRemoved), m_Moved(&board, &QAbstractItemModel::rowsMoved),
          m_Counts(&board, &BoardModel::countsChanged)
    {
    }

    /// No signal that changes the shape of the model.
    [[nodiscard]] bool shape_untouched() const
    {
        return m_Reset.isEmpty() && m_AboutToReset.isEmpty() && m_Layout.isEmpty() && m_Inserted.isEmpty() &&
               m_Removed.isEmpty() && m_Moved.isEmpty();
    }

    [[nodiscard]] int first_row(int signal = 0) const
    {
        return m_Data.at(signal).at(0).value<QModelIndex>().row();
    }
    [[nodiscard]] int last_row(int signal = 0) const
    {
        return m_Data.at(signal).at(1).value<QModelIndex>().row();
    }
    [[nodiscard]] QList<int> roles(int signal = 0) const
    {
        return m_Data.at(signal).at(2).value<QList<int>>();
    }

    QSignalSpy m_Data;
    QSignalSpy m_Reset;
    QSignalSpy m_AboutToReset;
    QSignalSpy m_Layout;
    QSignalSpy m_Inserted;
    QSignalSpy m_Removed;
    QSignalSpy m_Moved;
    QSignalSpy m_Counts;
};

const QList<int> COUNT_ROLES{BoardModel::COUNT, BoardModel::HAS_COUNT, BoardModel::ACCESSIBLE_NAME};

/// `count` types named "Test Type N" with no board position.
Configuration with_types(std::size_t count)
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.clear();
    for (std::size_t i = 0; i < count; ++i)
    {
        ErrorType type;
        type.m_Id = sample_id<ErrorTypeId>(500 + i);
        type.m_Name = "Test Type " + std::to_string(i + 1);
        configuration.m_ErrorTypes.push_back(std::move(type));
    }
    return configuration;
}

} // namespace

TEST_CASE("The board has twelve rows with the roles of the sample configuration", "[viewmodel][board]")
{
    BoardFixture fixture;
    const BoardModel& board = fixture.m_Board;

    REQUIRE(board.rowCount() == 12);
    CHECK(board.rowCount(board.index(0)) == 0); // a list has no children
    CHECK(board.total_count() == 0);

    const auto names = board.roleNames();
    CHECK(names.value(BoardModel::TYPE_ID) == "typeId");
    CHECK(names.value(BoardModel::LABEL) == "label");
    CHECK(names.value(BoardModel::COLOUR_SLOT) == "colourSlot");
    CHECK(names.value(BoardModel::COUNT) == "count");
    CHECK(names.value(BoardModel::HAS_COUNT) == "hasCount");
    CHECK(names.value(BoardModel::IS_OTHER) == "isOther");
    CHECK(names.value(BoardModel::IS_EMPTY) == "isEmpty");
    CHECK(names.value(BoardModel::ACCESSIBLE_NAME) == "accessibleName");
    CHECK(names.size() == 8);

    const Configuration& configuration = fixture.configuration();
    for (int row = 0; row < 11; ++row)
    {
        const ErrorType& type = configuration.m_ErrorTypes[static_cast<std::size_t>(row)];
        CAPTURE(row);
        CHECK(fixture.value(row, BoardModel::TYPE_ID).toString() == fixture.type_text(type.m_Id));
        CHECK(fixture.value(row, BoardModel::LABEL).toString() == QString::fromStdString(type.m_Name));
        CHECK(fixture.value(row, BoardModel::COLOUR_SLOT).toInt() == row + 1);
        CHECK(fixture.count(row) == 0);
        CHECK_FALSE(fixture.value(row, BoardModel::HAS_COUNT).toBool());
        CHECK_FALSE(fixture.value(row, BoardModel::IS_OTHER).toBool());
        CHECK_FALSE(fixture.value(row, BoardModel::IS_EMPTY).toBool());
        CHECK(fixture.value(row, BoardModel::ACCESSIBLE_NAME).toString() == QString::fromStdString(type.m_Name));
    }

    CHECK(fixture.value(OTHER, BoardModel::TYPE_ID).toString().isEmpty());
    CHECK(fixture.value(OTHER, BoardModel::LABEL).toString() == QString::fromUtf8("Other\xE2\x80\xA6"));
    CHECK(fixture.value(OTHER, BoardModel::COLOUR_SLOT).toInt() == 0);
    CHECK(fixture.value(OTHER, BoardModel::IS_OTHER).toBool());
    CHECK_FALSE(fixture.value(OTHER, BoardModel::IS_EMPTY).toBool());
    CHECK(fixture.value(OTHER, BoardModel::ACCESSIBLE_NAME).toString() == "Other");

    CHECK_FALSE(board.data(board.index(12), BoardModel::LABEL).isValid());
    CHECK_FALSE(board.data(QModelIndex(), BoardModel::LABEL).isValid());
    CHECK_FALSE(fixture.value(0, Qt::UserRole + 99).isValid());
}

TEST_CASE("A configuration with nine types has empty tiles that nothing can tap", "[viewmodel][board]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.resize(9);
    BoardFixture fixture(std::move(configuration));

    REQUIRE(fixture.m_Board.rowCount() == 12);
    for (int row = 9; row < 11; ++row)
    {
        CAPTURE(row);
        CHECK(fixture.value(row, BoardModel::IS_EMPTY).toBool());
        CHECK(fixture.value(row, BoardModel::TYPE_ID).toString().isEmpty());
        CHECK(fixture.value(row, BoardModel::LABEL).toString().isEmpty());
        CHECK(fixture.value(row, BoardModel::COLOUR_SLOT).toInt() == 0);
        CHECK(fixture.count(row) == 0);
        CHECK_FALSE(fixture.value(row, BoardModel::HAS_COUNT).toBool());
        CHECK_FALSE(fixture.value(row, BoardModel::IS_OTHER).toBool());
    }
    CHECK_FALSE(fixture.value(8, BoardModel::IS_EMPTY).toBool());
    CHECK(fixture.value(OTHER, BoardModel::IS_OTHER).toBool()); // Other is the twelfth tile all the same
}

TEST_CASE("Before a configuration is in force the board is twelve tiles with only Other filled", "[viewmodel][board]")
{
    DocumentRig rig;
    FakeConfigurationSource configuration;
    FakeOpenLogSource open;
    BoardModel board(configuration, open);

    REQUIRE(board.rowCount() == 12);
    for (int row = 0; row < 11; ++row)
    {
        CHECK(board.data(board.index(row), BoardModel::IS_EMPTY).toBool());
    }
    CHECK(board.data(board.index(OTHER), BoardModel::IS_OTHER).toBool());
}

TEST_CASE("A tap raises its tile and not Other, and the signals follow the commit", "[viewmodel][board][feedback]")
{
    BoardFixture fixture;
    const Spies spies(fixture.m_Board);
    EventLog& events = fixture.m_Rig.events();
    QObject::connect(&fixture.m_Board, &QAbstractItemModel::dataChanged,
                     [&events] { events.record(EventKind::MARK, "board changed"); });

    const EntryId entry = fixture.tap(fixture.type(2)); // Wrong Track Played, tile 3

    // Held: the touch has been acknowledged and nothing else has happened.
    CHECK(fixture.m_Feedback.touches() == 1);
    CHECK(fixture.count(2) == 0);
    CHECK(fixture.m_Board.total_count() == 0);
    CHECK(spies.m_Data.isEmpty());
    CHECK(fixture.m_Feedback.recorded().empty());

    fixture.m_Rig.gateway().run_all();

    CHECK(fixture.count(2) == 1);
    CHECK(fixture.value(2, BoardModel::HAS_COUNT).toBool());
    CHECK(fixture.value(2, BoardModel::ACCESSIBLE_NAME).toString() == "Wrong Track Played, 1 today");
    CHECK(fixture.count(OTHER) == 0);
    CHECK(fixture.m_Board.total_count() == 1);

    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == 2);
    CHECK(spies.last_row() == 2);
    CHECK(spies.roles() == COUNT_ROLES);
    CHECK(spies.m_Counts.size() == 1);
    CHECK(spies.shape_untouched());

    // The order of the feedback: the impact on touch-down, the commit, the count, and the confirmation last.
    const std::string recorded = "entry_recorded " + entry.format();
    const Event touch{EventKind::HAPTIC, "impact"};
    const Event changed{EventKind::MARK, "board changed"};
    const Event confirmation{EventKind::CONFIRMATION, recorded};
    const auto commit = events.details(EventKind::COMMIT);
    REQUIRE(commit.size() == 1);
    CHECK(events.occurred_before(touch, Event{EventKind::COMMIT, commit.front()}));
    CHECK(events.occurred_before(Event{EventKind::COMMIT, commit.front()}, changed));
    CHECK(events.occurred_before(changed, confirmation));
    CHECK(events.count(EventKind::HAPTIC) == 1);
}

TEST_CASE("A tap on a type that is not on the board raises Other only", "[viewmodel][board]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[10].m_BoardPosition.reset(); // Navigation Failure leaves the board
    BoardFixture fixture(std::move(configuration));
    const Spies spies(fixture.m_Board);

    CHECK(fixture.value(10, BoardModel::IS_EMPTY).toBool());
    fixture.tap_and_commit(fixture.type(10));

    CHECK(fixture.count(OTHER) == 1);
    CHECK(fixture.value(OTHER, BoardModel::ACCESSIBLE_NAME).toString() == "Other, 1 today");
    for (int row = 0; row < 11; ++row)
    {
        CHECK(fixture.count(row) == 0);
    }
    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == OTHER);
    CHECK(spies.last_row() == OTHER);
    CHECK(fixture.m_Board.total_count() == 1);
}

TEST_CASE("Other counts a retired type that had entries", "[viewmodel][board]")
{
    BoardFixture fixture;
    fixture.tap_and_commit(fixture.type(4));
    fixture.tap_and_commit(fixture.type(4));
    CHECK(fixture.count(4) == 2);
    CHECK(fixture.count(OTHER) == 0);

    const Spies spies(fixture.m_Board);
    Configuration configuration = fixture.configuration();
    configuration.m_ErrorTypes[4].m_Retired = true;
    fixture.m_Config.set(std::move(configuration));

    CHECK(fixture.value(4, BoardModel::IS_EMPTY).toBool());
    CHECK(fixture.count(4) == 0);
    CHECK(fixture.count(OTHER) == 2);
    CHECK(fixture.m_Board.total_count() == 2);
    CHECK(spies.m_Data.size() == 1);
    CHECK(spies.shape_untouched());
}

TEST_CASE("A type moved off the board moves its entries into Other in one signal", "[viewmodel][board]")
{
    BoardFixture fixture;
    fixture.tap_and_commit(fixture.type(1));
    fixture.tap_and_commit(fixture.type(1));
    fixture.tap_and_commit(fixture.type(3));
    CHECK(fixture.count(1) == 2);

    const Spies spies(fixture.m_Board);
    Configuration configuration = fixture.configuration();
    configuration.m_ErrorTypes[1].m_BoardPosition.reset();
    fixture.m_Config.set(std::move(configuration));

    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == 0);
    CHECK(spies.last_row() == 11);
    CHECK(spies.roles().isEmpty()); // every role
    CHECK(fixture.count(1) == 0);
    CHECK(fixture.value(1, BoardModel::IS_EMPTY).toBool());
    CHECK(fixture.count(3) == 1);
    CHECK(fixture.count(OTHER) == 2);
    CHECK(fixture.m_Board.total_count() == 3);
    CHECK(spies.shape_untouched());
}

TEST_CASE("Undo lowers the count and a restore raises it", "[viewmodel][board]")
{
    BoardFixture fixture;
    fixture.tap_and_commit(fixture.type(7));
    const EntryId second = fixture.tap_and_commit(fixture.type(7));
    CHECK(fixture.count(7) == 2);

    const Spies spies(fixture.m_Board);
    fixture.undo(second);
    CHECK(fixture.count(7) == 1);
    CHECK(fixture.m_Board.total_count() == 1);
    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == 7);
    CHECK(spies.last_row() == 7);
    CHECK(spies.roles() == COUNT_ROLES);

    fixture.restore(second);
    CHECK(fixture.count(7) == 2);
    CHECK(fixture.m_Board.total_count() == 2);
    CHECK(spies.m_Data.size() == 2);

    // The last live entry deleted: the badge goes.
    const EntryId only = fixture.tap_and_commit(fixture.type(0));
    CHECK(fixture.value(0, BoardModel::HAS_COUNT).toBool());
    fixture.undo(only);
    CHECK_FALSE(fixture.value(0, BoardModel::HAS_COUNT).toBool());
    CHECK(fixture.value(0, BoardModel::ACCESSIBLE_NAME).toString() == "Wrong Response");
    CHECK(spies.shape_untouched());
}

TEST_CASE("An edit that changes the type of an entry moves one count in one signal", "[viewmodel][board]")
{
    BoardFixture fixture;
    const EntryId entry = fixture.tap_and_commit(fixture.type(2), 10); // tile 3
    CHECK(fixture.count(2) == 1);

    const Spies spies(fixture.m_Board);
    Entry edited = fixture.error_entry(fixture.type(8), 10); // tile 9
    REQUIRE(fixture.m_Rig.document->edit_entry(entry, std::move(edited), fixture.m_Rig.keep(fixture.m_Results)));
    fixture.m_Rig.gateway().run_all();

    CHECK(fixture.count(2) == 0);
    CHECK(fixture.count(8) == 1);
    CHECK(fixture.m_Board.total_count() == 1);
    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == 2);
    CHECK(spies.last_row() == 8);
    CHECK(spies.roles() == COUNT_ROLES);

    // An edit of the time only changes no tile and says nothing.
    Entry later = fixture.error_entry(fixture.type(8), 30);
    REQUIRE(fixture.m_Rig.document->edit_entry(entry, std::move(later), fixture.m_Rig.keep(fixture.m_Results)));
    fixture.m_Rig.gateway().run_all();
    CHECK(spies.m_Data.size() == 1);
    CHECK(spies.shape_untouched());
}

TEST_CASE("Reordering the board changes the tiles at the positions but not the colour of any type",
          "[viewmodel][board]")
{
    BoardFixture fixture;
    fixture.tap_and_commit(fixture.type(0));
    fixture.tap_and_commit(fixture.type(0));
    fixture.tap_and_commit(fixture.type(1));

    const QString first_id = fixture.type_text(fixture.type(0));
    const QString second_id = fixture.type_text(fixture.type(1));

    const Spies spies(fixture.m_Board);
    Configuration configuration = fixture.configuration();
    configuration.m_ErrorTypes[0].m_BoardPosition = 2;
    configuration.m_ErrorTypes[1].m_BoardPosition = 1;
    fixture.m_Config.set(std::move(configuration));

    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == 0);
    CHECK(spies.last_row() == 11);
    CHECK(spies.shape_untouched());

    CHECK(fixture.value(0, BoardModel::TYPE_ID).toString() == second_id);
    CHECK(fixture.value(0, BoardModel::LABEL).toString() == "Sequence Out of Order");
    CHECK(fixture.count(0) == 1);
    CHECK(fixture.value(1, BoardModel::TYPE_ID).toString() == first_id);
    CHECK(fixture.value(1, BoardModel::LABEL).toString() == "Wrong Response");
    CHECK(fixture.count(1) == 2);

    // The colour stayed with the type: Wrong Response is index 0 (slot 1) and Sequence Out of Order index 1 (slot 2).
    CHECK(fixture.value(0, BoardModel::COLOUR_SLOT).toInt() == 2);
    CHECK(fixture.value(1, BoardModel::COLOUR_SLOT).toInt() == 1);
    for (int row = 2; row < 11; ++row)
    {
        CHECK(fixture.value(row, BoardModel::COLOUR_SLOT).toInt() == row + 1);
    }
}

TEST_CASE("The twelfth and thirteenth types reuse slots 1 and 2 and Other is always slot 0", "[viewmodel][board]")
{
    Configuration configuration = with_types(13);
    // The first eleven types are off the board; the 12th and 13th are on positions 1 and 2, and the 5th on position 3.
    configuration.m_ErrorTypes[11].m_BoardPosition = 1;
    configuration.m_ErrorTypes[12].m_BoardPosition = 2;
    configuration.m_ErrorTypes[4].m_BoardPosition = 3;
    configuration.m_ErrorTypes[10].m_BoardPosition = 4;
    configuration.m_ErrorTypes[0].m_BoardPosition = 5;
    BoardFixture fixture(std::move(configuration));

    CHECK(fixture.value(0, BoardModel::LABEL).toString() == "Test Type 12");
    CHECK(fixture.value(0, BoardModel::COLOUR_SLOT).toInt() == 1);
    CHECK(fixture.value(1, BoardModel::LABEL).toString() == "Test Type 13");
    CHECK(fixture.value(1, BoardModel::COLOUR_SLOT).toInt() == 2);
    CHECK(fixture.value(2, BoardModel::COLOUR_SLOT).toInt() == 5);  // the 5th type
    CHECK(fixture.value(3, BoardModel::COLOUR_SLOT).toInt() == 11); // the 11th type: the last slot
    CHECK(fixture.value(4, BoardModel::COLOUR_SLOT).toInt() == 1);  // the 1st type
    CHECK(fixture.value(OTHER, BoardModel::COLOUR_SLOT).toInt() == 0);

    // The rule is the position in the master list, retired types included: retiring the first type does not move the
    // second type's colour to slot 1.
    Configuration with_retired = fixture.configuration();
    with_retired.m_ErrorTypes[0].m_Retired = true;
    with_retired.m_ErrorTypes[1].m_BoardPosition = 5;
    fixture.m_Config.set(std::move(with_retired));
    CHECK(fixture.value(4, BoardModel::LABEL).toString() == "Test Type 2");
    CHECK(fixture.value(4, BoardModel::COLOUR_SLOT).toInt() == 2);
    CHECK(fixture.value(0, BoardModel::COLOUR_SLOT).toInt() == 1);

    CHECK(viewmodel::tile_colour_slot(0) == 1);
    CHECK(viewmodel::tile_colour_slot(10) == 11);
    CHECK(viewmodel::tile_colour_slot(11) == 1);
    CHECK(viewmodel::tile_colour_slot(12) == 2);
    CHECK(viewmodel::tile_colour_slot(22) == 1);
}

TEST_CASE("Signal counts: one per commit, one for a new log, one for a configuration change, none for a failure",
          "[viewmodel][board]")
{
    BoardFixture fixture;
    const Spies spies(fixture.m_Board);

    SECTION("a commit")
    {
        fixture.tap_and_commit(fixture.type(0));
        CHECK(spies.m_Data.size() == 1);
        fixture.tap_and_commit(fixture.type(0));
        CHECK(spies.m_Data.size() == 2);
    }

    SECTION("several taps in flight are one signal each, as they commit")
    {
        fixture.tap(fixture.type(0));
        fixture.tap(fixture.type(1));
        fixture.tap(fixture.type(2));
        CHECK(spies.m_Data.isEmpty());
        fixture.m_Rig.gateway().run_next();
        CHECK(spies.m_Data.size() == 1);
        fixture.m_Rig.gateway().run_all();
        CHECK(spies.m_Data.size() == 3);
        CHECK(fixture.m_Board.total_count() == 3);
    }

    SECTION("a failed write tells nobody")
    {
        fixture.m_Rig.storage().repository().fail_next(RepositoryOp::ADD_ENTRY);
        fixture.tap(fixture.type(0));
        fixture.m_Rig.gateway().run_all();

        CHECK(spies.m_Data.isEmpty());
        CHECK(spies.m_Counts.isEmpty());
        CHECK(fixture.count(0) == 0);
        CHECK(fixture.m_Board.total_count() == 0);
        CHECK(fixture.m_Feedback.recorded().empty());
        CHECK(fixture.m_Feedback.touches() == 1); // the finger was answered all the same

        fixture.tap_and_commit(fixture.type(0)); // and the next tap works
        CHECK(spies.m_Data.size() == 1);
        CHECK(fixture.count(0) == 1);
    }

    SECTION("a configuration change")
    {
        Configuration configuration = fixture.configuration();
        configuration.m_ErrorTypes[0].m_Name = "Wrong Answer";
        fixture.m_Config.set(std::move(configuration));

        REQUIRE(spies.m_Data.size() == 1);
        CHECK(spies.first_row() == 0);
        CHECK(spies.last_row() == 11);
        CHECK(spies.roles().isEmpty());
        CHECK(fixture.value(0, BoardModel::LABEL).toString() == "Wrong Answer");
    }

    SECTION("a new log")
    {
        fixture.tap_and_commit(fixture.type(0));
        fixture.tap_and_commit(fixture.type(1));
        CHECK(spies.m_Data.size() == 2);

        const LogId next_log = sample_id<LogId>(2);
        REQUIRE(fixture.m_Rig.storage().memory().start_new_log(fixture.m_Rig.log_id(), next_log, sample_header()));
        auto replacement = fixture.m_Rig.storage().memory().load_log(next_log);
        REQUIRE(replacement.has_value());
        fixture.m_Rig.document->replace(std::move(*replacement));

        REQUIRE(spies.m_Data.size() == 3);
        CHECK(spies.first_row(2) == 0);
        CHECK(spies.last_row(2) == 11);
        CHECK(spies.roles(2).isEmpty());
        CHECK(fixture.count(0) == 0);
        CHECK(fixture.count(1) == 0);
        CHECK(fixture.m_Board.total_count() == 0);
    }

    SECTION("a note, a header correction and a share change no tile")
    {
        Entry note = sample_note(900, 20, "Played Morning Medley");
        REQUIRE(
            fixture.m_Rig.document->add_entry(std::move(note), Origin::USER, fixture.m_Rig.keep(fixture.m_Results)));
        REQUIRE(fixture.m_Rig.document->correct_header(HeaderField::UNIT_NUMBER, "28",
                                                       fixture.m_Rig.keep(fixture.m_Results)));
        REQUIRE(fixture.m_Rig.document->mark_shared(sample_time(30), fixture.m_Rig.keep(fixture.m_Results)));
        fixture.m_Rig.gateway().run_all();

        CHECK(fixture.m_Results.size() == 3);
        CHECK(spies.m_Data.isEmpty());
        CHECK(fixture.m_Board.total_count() == 0);
    }

    CHECK(spies.shape_untouched());
}

TEST_CASE("Only the open document drives the board", "[viewmodel][board]")
{
    BoardFixture fixture;
    fixture.tap_and_commit(fixture.type(0));
    const Spies spies(fixture.m_Board);

    // An archived log of a past day, held by History or Review: its own storage, its own document.
    DocumentRig past;
    REQUIRE(past.document->add_entry(sample_error(1, 10), Origin::USER, past.keep(fixture.m_Results)));
    REQUIRE(past.document->add_entry(sample_error(2, 11), Origin::USER, past.keep(fixture.m_Results)));
    past.gateway().run_all();
    past.document->set_archived();
    REQUIRE(past.document->set_deleted(sample_id<EntryId>(1), true, Origin::USER, past.keep(fixture.m_Results)));
    past.gateway().run_all();

    CHECK(past.document->log().entries().size() == 2);
    CHECK(spies.m_Data.isEmpty());
    CHECK(fixture.count(0) == 1);
    CHECK(fixture.m_Board.total_count() == 1);
}

TEST_CASE("The board follows another document when the open log is replaced by one", "[viewmodel][board]")
{
    BoardFixture fixture;
    fixture.tap_and_commit(fixture.type(0));
    CHECK(fixture.count(0) == 1);

    DocumentRig other;
    REQUIRE(other.document->add_entry(sample_error(5, 10), Origin::USER, other.keep(fixture.m_Results)));
    REQUIRE(other.document->add_entry(sample_error(6, 11), Origin::USER, other.keep(fixture.m_Results)));
    other.gateway().run_all();
    // The sample entries are of a type the sample configuration does not know: they are Other's.

    const Spies spies(fixture.m_Board);
    fixture.m_Open.set(other.document.get());

    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.first_row() == 0);
    CHECK(spies.last_row() == 11);
    CHECK(fixture.count(0) == 0);
    CHECK(fixture.count(OTHER) == 2);
    CHECK(fixture.m_Board.total_count() == 2);
    CHECK(spies.shape_untouched());

    // The document that was open no longer drives the board; the new one does.
    fixture.tap_and_commit(fixture.type(0));
    CHECK(spies.m_Data.size() == 1);
    CHECK(fixture.count(0) == 0);
    REQUIRE(other.document->add_entry(sample_error(7, 12), Origin::USER, other.keep(fixture.m_Results)));
    other.gateway().run_all();
    CHECK(spies.m_Data.size() == 2);
    CHECK(fixture.count(OTHER) == 3);

    // No open log: every count is zero.
    fixture.m_Open.set(nullptr);
    CHECK(spies.m_Data.size() == 3);
    CHECK(fixture.count(OTHER) == 0);
    CHECK(fixture.m_Board.total_count() == 0);
    CHECK(spies.shape_untouched());
}

TEST_CASE("The board counts entries of a log it is given at start-up and a document that loads later",
          "[viewmodel][board]")
{
    DocumentRig rig(false);
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(1, 10)));
    FakeConfigurationSource configuration;
    FakeOpenLogSource open;
    configuration.set(sample_configuration());

    auto document = std::make_shared<viewmodel::LogDocument>(rig.world().environment(), rig.gateway(), rig.log_id());
    open.set(document.get());
    BoardModel board(configuration, open);
    CHECK(board.total_count() == 0); // not ready: no log yet

    const QSignalSpy data(&board, &QAbstractItemModel::dataChanged);
    document->load(rig.stored());

    CHECK(data.size() == 1); // LOADED is one notification
    CHECK(board.total_count() == 1);
}

TEST_CASE("Deleted entries and non-error entries are never counted", "[viewmodel][board]")
{
    DocumentRig rig(false);
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(1, 10)));
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_error(2, 11)));
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_note(3, 12, "A note")));
    REQUIRE(rig.storage().memory().add_entry(rig.log_id(), sample_change(4, 13, HeaderField::UNIT_NUMBER, "28")));
    REQUIRE(rig.storage().memory().set_entry_deleted(rig.log_id(), sample_id<EntryId>(2), true));
    Configuration configuration = with_types(1);
    configuration.m_ErrorTypes[0].m_Id = sample_id<ErrorTypeId>(1); // the sample entries' type
    configuration.m_ErrorTypes[0].m_BoardPosition = 1;
    FakeConfigurationSource source;
    source.set(std::move(configuration));
    viewmodel::LogDocument document(rig.world().environment(), rig.gateway(), rig.stored());
    FakeOpenLogSource open;
    open.set(&document);

    const BoardModel board(source, open);

    CHECK(board.total_count() == 1);
    CHECK(board.data(board.index(0), BoardModel::COUNT).toInt() == 1);
    CHECK(board.data(board.index(OTHER), BoardModel::COUNT).toInt() == 0);
}

TEST_CASE("The board takes itself off the sources when it goes", "[viewmodel][board]")
{
    DocumentRig rig;
    FakeConfigurationSource configuration;
    FakeOpenLogSource open;
    open.set(rig.document.get());
    configuration.set(sample_configuration());
    {
        const BoardModel board(configuration, open);
        CHECK(configuration.observer_count() == 1);
        CHECK(open.observer_count() == 1);
    }
    CHECK(configuration.observer_count() == 0);
    CHECK(open.observer_count() == 0);

    // Nothing is called back: a change after the board has gone is safe.
    int done = 0;
    configuration.set(sample_configuration());
    open.set(nullptr);
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.count(done)));
    rig.gateway().run_all();
    CHECK(done == 1);
}
