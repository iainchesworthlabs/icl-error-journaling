// SPDX-License-Identifier: GPL-3.0-or-later
#include "storage_samples.hpp"
#include "support/document_rig.hpp"
#include "support/fake_sources.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/error_picker_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QList>
#include <QModelIndex>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::ErrorPickerModel;
using viewmodel::LogChange;
using Origin = LogChange::Origin;

namespace
{

constexpr int OFF = ErrorPickerModel::OFF_BOARD;
constexpr int ON = ErrorPickerModel::ON_BOARD;

/// The sample configuration with Navigation Failure taken off the board, a new "Écho Delay" that was never on it, and a
/// retired type. Master list indices: 0 to 10 the sample types, 11 Écho Delay, 12 the retired type.
Configuration picker_configuration()
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[10].m_BoardPosition.reset();

    ErrorType echo;
    echo.m_Id = sample_id<ErrorTypeId>(600);
    echo.m_Name = "\xC3\x89"
                  "cho Delay"; // "Écho Delay"
    configuration.m_ErrorTypes.push_back(std::move(echo));

    ErrorType retired;
    retired.m_Id = sample_id<ErrorTypeId>(601);
    retired.m_Name = "Retired Type";
    retired.m_Retired = true;
    configuration.m_ErrorTypes.push_back(std::move(retired));
    return configuration;
}

struct PickerFixture
{
    explicit PickerFixture(Configuration configuration = picker_configuration())
        : m_Editor(m_Rig.world().m_Ids, &m_Config), m_Picker(m_Config, m_Open, m_Editor)
    {
        m_Config.set(std::move(configuration));
        m_Open.set(m_Rig.document.get());
    }

    PickerFixture(const PickerFixture&) = delete;
    PickerFixture& operator=(const PickerFixture&) = delete;

    DocumentRig m_Rig;
    FakeConfigurationSource m_Config;
    FakeOpenLogSource m_Open;
    FakeConfigurationEditor m_Editor;
    FakeFeedbackSink m_Feedback{&m_Rig.events()};
    ErrorPickerModel m_Picker;
    std::uint64_t m_NextEntry = 1;
    std::vector<Result<void, StorageError>> m_Results;

    [[nodiscard]] ErrorTypeId type(std::size_t master_index) const
    {
        return m_Config.configuration().m_ErrorTypes.at(master_index).m_Id;
    }

    EntryId tap_and_commit(ErrorTypeId type_id)
    {
        Entry entry;
        entry.m_Id = sample_id<EntryId>(m_NextEntry++);
        entry.m_Captured = Timestamp{sample_time(10), "Australia/Melbourne"};
        entry.m_Body = ErrorEntry{type_id, std::nullopt, "label"};
        const EntryId id = entry.m_Id;
        REQUIRE(m_Rig.document->add_entry(std::move(entry), Origin::USER, m_Rig.keep(m_Results)));
        m_Rig.gateway().run_all();
        return id;
    }

    [[nodiscard]] QVariant value(int row, int role) const
    {
        return m_Picker.data(m_Picker.index(row), role);
    }
    [[nodiscard]] QString label(int row) const
    {
        return value(row, ErrorPickerModel::LABEL).toString();
    }
    [[nodiscard]] QStringList labels() const
    {
        QStringList all;
        for (int row = 0; row < m_Picker.rowCount(); ++row)
        {
            all.push_back(label(row));
        }
        return all;
    }
};

struct PickerSpies
{
    explicit PickerSpies(ErrorPickerModel& picker)
        : m_Data(&picker, &QAbstractItemModel::dataChanged), m_Reset(&picker, &QAbstractItemModel::modelReset),
          m_AboutToReset(&picker, &QAbstractItemModel::modelAboutToBeReset),
          m_Layout(&picker, &QAbstractItemModel::layoutChanged), m_Inserted(&picker, &QAbstractItemModel::rowsInserted),
          m_Removed(&picker, &QAbstractItemModel::rowsRemoved), m_Filter(&picker, &ErrorPickerModel::filterChanged),
          m_Error(&picker, &ErrorPickerModel::addTypeErrorChanged)
    {
    }

    QSignalSpy m_Data;
    QSignalSpy m_Reset;
    QSignalSpy m_AboutToReset;
    QSignalSpy m_Layout;
    QSignalSpy m_Inserted;
    QSignalSpy m_Removed;
    QSignalSpy m_Filter;
    QSignalSpy m_Error;
};

} // namespace

TEST_CASE("The picker lists the live types in two sections with today's counts", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const ErrorPickerModel& picker = fixture.m_Picker;

    // Not on your board first, then on your board, each in master-list order; the retired type is absent.
    REQUIRE(picker.rowCount() == 12);
    CHECK(fixture.label(0) == "Navigation Failure");
    CHECK(fixture.label(1) == "\xC3\x89"
                              "cho Delay");
    CHECK(fixture.label(2) == "Wrong Response");
    CHECK(fixture.label(11) == "Track Delay");
    CHECK_FALSE(fixture.labels().contains("Retired Type"));

    CHECK(fixture.value(0, ErrorPickerModel::SECTION).toInt() == OFF);
    CHECK(fixture.value(1, ErrorPickerModel::SECTION).toInt() == OFF);
    for (int row = 2; row < 12; ++row)
    {
        CHECK(fixture.value(row, ErrorPickerModel::SECTION).toInt() == ON);
    }

    CHECK(fixture.value(0, ErrorPickerModel::TYPE_ID).toString() == QString::fromStdString(fixture.type(10).format()));
    CHECK(fixture.value(1, ErrorPickerModel::TYPE_ID).toString() == QString::fromStdString(fixture.type(11).format()));

    // The colour of a type is the tile's rule: its index in the master list modulo 11, plus one.
    CHECK(fixture.value(0, ErrorPickerModel::COLOUR_SLOT).toInt() == 11);
    CHECK(fixture.value(1, ErrorPickerModel::COLOUR_SLOT).toInt() == 1);
    CHECK(fixture.value(2, ErrorPickerModel::COLOUR_SLOT).toInt() == 1);
    CHECK(fixture.value(3, ErrorPickerModel::COLOUR_SLOT).toInt() == 2);

    for (int row = 0; row < 12; ++row)
    {
        CHECK(fixture.value(row, ErrorPickerModel::COUNT).toInt() == 0);
    }

    const auto names = picker.roleNames();
    CHECK(names.value(ErrorPickerModel::TYPE_ID) == "typeId");
    CHECK(names.value(ErrorPickerModel::LABEL) == "label");
    CHECK(names.value(ErrorPickerModel::SECTION) == "section");
    CHECK(names.value(ErrorPickerModel::COUNT) == "count");
    CHECK(names.value(ErrorPickerModel::COLOUR_SLOT) == "colourSlot");
    CHECK(names.size() == 5);

    CHECK(picker.rowCount(picker.index(0)) == 0);
    CHECK_FALSE(picker.data(picker.index(12), ErrorPickerModel::LABEL).isValid());
    CHECK_FALSE(picker.data(QModelIndex(), ErrorPickerModel::LABEL).isValid());
    CHECK(picker.filter().isEmpty());
    CHECK(picker.add_type_error().isEmpty());
}

TEST_CASE("The sample configuration has every type on the board", "[viewmodel][picker]")
{
    PickerFixture fixture(sample_configuration());

    REQUIRE(fixture.m_Picker.rowCount() == 11);
    for (int row = 0; row < 11; ++row)
    {
        CHECK(fixture.value(row, ErrorPickerModel::SECTION).toInt() == ON);
    }
}

TEST_CASE("Counts follow commits, undo and a new log", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const PickerSpies spies(fixture.m_Picker);

    fixture.tap_and_commit(fixture.type(0)); // Wrong Response, on the board: row 2
    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 1);
    REQUIRE(spies.m_Data.size() == 1);
    CHECK(spies.m_Data.at(0).at(0).value<QModelIndex>().row() == 2);
    CHECK(spies.m_Data.at(0).at(1).value<QModelIndex>().row() == 2);
    CHECK(spies.m_Data.at(0).at(2).value<QList<int>>() == QList<int>{ErrorPickerModel::COUNT});

    const EntryId off_board = fixture.tap_and_commit(fixture.type(11)); // Écho Delay, off the board: row 1
    CHECK(fixture.value(1, ErrorPickerModel::COUNT).toInt() == 1);
    CHECK(spies.m_Data.size() == 2);

    REQUIRE(fixture.m_Rig.document->set_deleted(off_board, true, Origin::UNDO, fixture.m_Rig.keep(fixture.m_Results)));
    fixture.m_Rig.gateway().run_all();
    CHECK(fixture.value(1, ErrorPickerModel::COUNT).toInt() == 0);
    CHECK(spies.m_Data.size() == 3);

    // A commit that touches no count says nothing.
    REQUIRE(fixture.m_Rig.document->mark_shared(sample_time(40), fixture.m_Rig.keep(fixture.m_Results)));
    fixture.m_Rig.gateway().run_all();
    CHECK(spies.m_Data.size() == 3);

    // A new log starts the counts again, in one signal.
    const LogId next_log = sample_id<LogId>(2);
    REQUIRE(fixture.m_Rig.storage().memory().start_new_log(fixture.m_Rig.log_id(), next_log, sample_header()));
    auto replacement = fixture.m_Rig.storage().memory().load_log(next_log);
    REQUIRE(replacement.has_value());
    fixture.m_Rig.document->replace(std::move(*replacement));
    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 0);
    CHECK(spies.m_Data.size() == 4);

    CHECK(spies.m_Reset.isEmpty());
    CHECK(spies.m_Layout.isEmpty());
}

TEST_CASE("A failed write changes no count", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const PickerSpies spies(fixture.m_Picker);

    fixture.m_Rig.storage().repository().fail_next(RepositoryOp::ADD_ENTRY);
    fixture.tap_and_commit(fixture.type(0));

    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 0);
    CHECK(spies.m_Data.isEmpty());
    CHECK(spies.m_Reset.isEmpty());
}

TEST_CASE("The filter is a case-insensitive substring match with one reset per change", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const PickerSpies spies(fixture.m_Picker);
    ErrorPickerModel& picker = fixture.m_Picker;

    SECTION("case")
    {
        picker.set_filter("DELAY");
        CHECK(fixture.labels() == QStringList{"\xC3\x89"
                                              "cho Delay",
                                              "Mute Delay", "Speech Delay", "Track Delay"});
        picker.set_filter("mute");
        CHECK(fixture.labels() == QStringList{"Mute Delay"});
        picker.set_filter("ute DEL");
        CHECK(fixture.labels() == QStringList{"Mute Delay"});
    }

    SECTION("letters beyond ASCII fold too, and accents are not removed")
    {
        picker.set_filter("\xC3\xA9"
                          "CHO"); // "écHO"
        CHECK(fixture.labels() == QStringList{"\xC3\x89"
                                              "cho Delay"});
        picker.set_filter("echo"); // an unaccented e is not an accented one
        CHECK(picker.rowCount() == 0);
    }

    SECTION("a filter that matches nothing leaves an empty list")
    {
        picker.set_filter("zebra");
        CHECK(picker.rowCount() == 0);
        CHECK(fixture.labels().isEmpty());
    }

    SECTION("a filter that matches only a type on the board")
    {
        picker.set_filter("response");
        REQUIRE(picker.rowCount() == 1);
        CHECK(fixture.label(0) == "Wrong Response");
        CHECK(fixture.value(0, ErrorPickerModel::SECTION).toInt() == ON);
    }

    SECTION("a filter that matches only a type that is not on the board")
    {
        picker.set_filter("navigation");
        REQUIRE(picker.rowCount() == 1);
        CHECK(fixture.value(0, ErrorPickerModel::SECTION).toInt() == OFF);
    }

    SECTION("a retired type is not found")
    {
        picker.set_filter("retired");
        CHECK(picker.rowCount() == 0);
    }

    SECTION("white space around the filter is ignored and an empty filter lists everything")
    {
        picker.set_filter("  mute ");
        CHECK(fixture.labels() == QStringList{"Mute Delay"});
        CHECK(picker.filter() == "  mute "); // the property reads back what was typed
        picker.set_filter("   ");
        CHECK(picker.rowCount() == 12);
        picker.set_filter({});
        CHECK(picker.rowCount() == 12);
    }

    SECTION("counts are on the filtered rows")
    {
        fixture.tap_and_commit(fixture.type(7)); // Mute Delay
        picker.set_filter("mute");
        REQUIRE(picker.rowCount() == 1);
        CHECK(fixture.value(0, ErrorPickerModel::COUNT).toInt() == 1);
        fixture.tap_and_commit(fixture.type(7));
        CHECK(fixture.value(0, ErrorPickerModel::COUNT).toInt() == 2);
    }

    CHECK(spies.m_Layout.isEmpty());
}

TEST_CASE("One modelReset per filter change and filterChanged only when it changed", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const PickerSpies spies(fixture.m_Picker);
    ErrorPickerModel& picker = fixture.m_Picker;

    picker.set_filter("track");
    CHECK(spies.m_Reset.size() == 1);
    CHECK(spies.m_AboutToReset.size() == 1);
    CHECK(spies.m_Filter.size() == 1);

    picker.set_filter("track"); // the same text: nothing
    CHECK(spies.m_Reset.size() == 1);
    CHECK(spies.m_Filter.size() == 1);

    picker.set_filter("tracks"); // matches nothing, still one reset
    CHECK(spies.m_Reset.size() == 2);
    CHECK(spies.m_Filter.size() == 2);

    picker.set_filter({});
    CHECK(spies.m_Reset.size() == 3);
    CHECK(spies.m_Filter.size() == 3);
    CHECK(spies.m_Data.isEmpty());
    CHECK(spies.m_Inserted.isEmpty());
    CHECK(spies.m_Removed.isEmpty());
}

TEST_CASE("addType shows the new type under Not on your board once the save has committed", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const PickerSpies spies(fixture.m_Picker);
    ErrorPickerModel& picker = fixture.m_Picker;
    const int before = picker.rowCount();

    const QString id = picker.addType("  Wrong Greeting ");

    REQUIRE_FALSE(id.isEmpty());
    REQUIRE(fixture.m_Editor.added().size() == 1);
    CHECK(fixture.m_Editor.added().front().m_Name == "Wrong Greeting"); // trimmed
    CHECK(QString::fromStdString(fixture.m_Editor.added().front().m_Id.format()) == id);
    CHECK(picker.add_type_error().isEmpty());

    // The save has not committed: the model does not have the type yet.
    CHECK(picker.rowCount() == before);
    CHECK(spies.m_Reset.isEmpty());

    fixture.m_Editor.commit();

    REQUIRE(picker.rowCount() == before + 1);
    CHECK(spies.m_Reset.size() == 1);
    // At the end of the first section, before the types that are on the board.
    CHECK(fixture.label(2) == "Wrong Greeting");
    CHECK(fixture.value(2, ErrorPickerModel::TYPE_ID).toString() == id);
    CHECK(fixture.value(2, ErrorPickerModel::SECTION).toInt() == OFF);
    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 0);
    // The 14th type of the master list (index 13): slot 13 modulo 11, plus one.
    CHECK(fixture.value(2, ErrorPickerModel::COLOUR_SLOT).toInt() == 3);
    CHECK(fixture.label(3) == "Wrong Response");
}

TEST_CASE("addType does not record an entry", "[viewmodel][picker][feedback]")
{
    PickerFixture fixture;

    const QString id = fixture.m_Picker.addType("Wrong Greeting");
    fixture.m_Editor.commit();
    fixture.m_Rig.gateway().run_all();

    REQUIRE_FALSE(id.isEmpty());
    CHECK(fixture.m_Rig.gateway().posted() == 0);
    CHECK(fixture.m_Rig.document->log().entries().empty());
    CHECK(fixture.m_Rig.document->pending() == 0);
    CHECK(fixture.m_Feedback.touches() == 0);
    CHECK(fixture.m_Feedback.recorded().empty());
    CHECK(fixture.m_Rig.events().count(EventKind::HAPTIC) == 0);
    CHECK(fixture.m_Rig.events().count(EventKind::COMMIT) == 0);

    // The facilitator taps it as for any other type; the count then follows the commit.
    const ErrorTypeId added = fixture.m_Editor.added().front().m_Id;
    fixture.tap_and_commit(added);
    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 1);
}

TEST_CASE("addType refuses what the editor refuses and says why", "[viewmodel][picker]")
{
    PickerFixture fixture;
    const PickerSpies spies(fixture.m_Picker);
    ErrorPickerModel& picker = fixture.m_Picker;
    const int before = picker.rowCount();

    SECTION("an empty name, and a name of spaces")
    {
        CHECK(picker.addType("").isEmpty());
        CHECK_FALSE(picker.add_type_error().isEmpty());
        CHECK(spies.m_Error.size() == 1);
        CHECK(picker.addType("   \t").isEmpty());
        CHECK(spies.m_Error.size() == 1); // the same message again is not a new one
    }

    SECTION("a refusal of the rules: a duplicate, an over-long name, a full list")
    {
        for (const char* message :
             {"That name is used already.", "The name is over 200 characters.", "The list is full."})
        {
            fixture.m_Editor.refuse_next(
                viewmodel::ConfigEditError{viewmodel::ConfigEditError::Code::REFUSED, message});
            CHECK(picker.addType("Mute delay").isEmpty());
            CHECK(picker.add_type_error() == message);
        }
    }

    SECTION("a store that is not ready")
    {
        fixture.m_Editor.refuse_next(
            viewmodel::ConfigEditError{viewmodel::ConfigEditError::Code::NOT_READY, "Settings are not loaded."});
        CHECK(picker.addType("Wrong Greeting").isEmpty());
        CHECK(picker.add_type_error() == "Settings are not loaded.");
    }

    SECTION("a success clears the message")
    {
        CHECK(picker.addType("").isEmpty());
        CHECK_FALSE(picker.add_type_error().isEmpty());
        CHECK_FALSE(picker.addType("Wrong Greeting").isEmpty());
        CHECK(picker.add_type_error().isEmpty());
        CHECK(spies.m_Error.size() == 2);
    }

    fixture.m_Editor.commit();
    CHECK(picker.rowCount() == (fixture.m_Editor.added().empty() ? before : before + 1));
}

TEST_CASE("A configuration change keeps the filter and resets only when the list changes", "[viewmodel][picker]")
{
    PickerFixture fixture;
    ErrorPickerModel& picker = fixture.m_Picker;
    picker.set_filter("delay");
    REQUIRE(picker.rowCount() == 4);
    const PickerSpies spies(picker);

    SECTION("a setting that is not on the list changes nothing")
    {
        Configuration configuration = fixture.m_Config.configuration();
        configuration.m_Facilitator = "Sam Example";
        configuration.m_IncludeTotals = true;
        fixture.m_Config.set(std::move(configuration));
        CHECK(spies.m_Reset.isEmpty());
        CHECK(spies.m_Data.isEmpty());
        CHECK(picker.rowCount() == 4);
    }

    SECTION("a type that matches the filter is added")
    {
        picker.addType("Greeting Delay");
        fixture.m_Editor.commit();
        CHECK(spies.m_Reset.size() == 1);
        REQUIRE(picker.rowCount() == 5);
        CHECK(fixture.label(1) == "Greeting Delay");
        CHECK(picker.filter() == "delay");
    }

    SECTION("a type that does not match the filter is added")
    {
        picker.addType("Wrong Greeting");
        fixture.m_Editor.commit();
        CHECK(picker.rowCount() == 4);
    }

    SECTION("a type moves between the sections")
    {
        Configuration configuration = fixture.m_Config.configuration();
        configuration.m_ErrorTypes[7].m_BoardPosition.reset(); // Mute Delay leaves the board
        fixture.m_Config.set(std::move(configuration));
        CHECK(spies.m_Reset.size() == 1);
        REQUIRE(picker.rowCount() == 4);
        CHECK(fixture.label(0) == "Mute Delay");
        CHECK(fixture.label(1) == "É"
                                  "cho Delay");
        CHECK(fixture.value(0, ErrorPickerModel::SECTION).toInt() == OFF);
    }

    SECTION("a type is retired")
    {
        Configuration configuration = fixture.m_Config.configuration();
        configuration.m_ErrorTypes[7].m_Retired = true;
        fixture.m_Config.set(std::move(configuration));
        CHECK(spies.m_Reset.size() == 1);
        CHECK(picker.rowCount() == 3);
    }
}

TEST_CASE("The picker follows the open log it is given and takes itself off the sources when it goes",
          "[viewmodel][picker]")
{
    DocumentRig rig;
    FakeConfigurationSource configuration;
    FakeOpenLogSource open;
    FakeConfigurationEditor editor(rig.world().m_Ids, &configuration);
    configuration.set(sample_configuration());
    open.set(rig.document.get());
    {
        const ErrorPickerModel picker(configuration, open, editor);
        CHECK(configuration.observer_count() == 1);
        CHECK(open.observer_count() == 1);
        CHECK(picker.rowCount() == 11);
    }
    CHECK(configuration.observer_count() == 0);
    CHECK(open.observer_count() == 0);

    // Nothing is called back after it has gone.
    int done = 0;
    configuration.set(sample_configuration());
    open.set(nullptr);
    REQUIRE(rig.document->add_entry(sample_error(1, 10), Origin::USER, rig.count(done)));
    rig.gateway().run_all();
    CHECK(done == 1);
}

TEST_CASE("Only the open document gives the picker its counts", "[viewmodel][picker]")
{
    DocumentRig other; // outlives the picker that follows it
    PickerFixture fixture;
    fixture.tap_and_commit(fixture.type(0));
    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 1);

    Entry entry = sample_error(5, 10);
    entry.m_Body = ErrorEntry{fixture.type(1), std::nullopt, "Sequence Out of Order"};
    REQUIRE(other.document->add_entry(std::move(entry), Origin::USER, other.keep(fixture.m_Results)));
    other.gateway().run_all();

    const PickerSpies spies(fixture.m_Picker);
    CHECK(spies.m_Data.isEmpty());
    CHECK(fixture.value(3, ErrorPickerModel::COUNT).toInt() == 0);

    fixture.m_Open.set(other.document.get());
    CHECK(fixture.value(2, ErrorPickerModel::COUNT).toInt() == 0);
    CHECK(fixture.value(3, ErrorPickerModel::COUNT).toInt() == 1);
    CHECK(spies.m_Data.size() == 1);
    CHECK(spies.m_Reset.isEmpty());
}
