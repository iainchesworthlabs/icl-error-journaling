// SPDX-License-Identifier: GPL-3.0-or-later
//
// SettingsModel's lists: locations, error types, the board layout and note templates (work package 3.3). Unit tests on
// the fake store and the manual gateway.
#include "settings_rig.hpp"
#include "viewmodel/board_layout_model.hpp"
#include "viewmodel/board_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using viewmodel::SettingsModel;

namespace
{

QString id_of(const ErrorType& type)
{
    return QString::fromStdString(type.m_Id.format());
}

QString id_of(const Location& location)
{
    return QString::fromStdString(location.m_Id.format());
}

QString id_of(const NoteTemplate& note_template)
{
    return QString::fromStdString(note_template.m_Id.format());
}

/// The names in the rows of a model, top to bottom.
QStringList names_in(const QAbstractItemModel& model)
{
    const int role = role_named(model, "name");
    QStringList names;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        names.push_back(text_at(model, row, role));
    }
    return names;
}

/// The labels of the tiles of the board layout, top to bottom.
QStringList labels_in(const QAbstractItemModel& model)
{
    const int role = role_named(model, "label");
    QStringList labels;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        labels.push_back(text_at(model, row, role));
    }
    return labels;
}

/// The colour slot of a type in the `errorTypes` model, found by its id.
int slot_of(const QAbstractItemModel& types, const QString& type_id)
{
    for (int row = 0; row < types.rowCount(); ++row)
    {
        if (text_at(types, row, role_named(types, "typeId")) == type_id)
        {
            return number_at(types, row, role_named(types, "colourSlot"));
        }
    }
    return -1;
}

Configuration with_locations(int count)
{
    Configuration configuration = sample_configuration();
    configuration.m_Locations.clear();
    for (int i = 0; i < count; ++i)
    {
        Location location;
        location.m_Id = test_id<LocationId>(5000 + static_cast<std::uint64_t>(i));
        location.m_Name = "Site " + std::to_string(i);
        location.m_SortOrder = i + 1;
        configuration.m_Locations.push_back(std::move(location));
    }
    return configuration;
}

Configuration with_error_types(int count)
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.clear();
    for (int i = 0; i < count; ++i)
    {
        ErrorType type;
        type.m_Id = test_id<ErrorTypeId>(6000 + static_cast<std::uint64_t>(i));
        type.m_Name = "Type " + std::to_string(i);
        if (i < 11)
        {
            type.m_BoardPosition = i + 1;
        }
        configuration.m_ErrorTypes.push_back(std::move(type));
    }
    return configuration;
}

} // namespace

// ------------------------------------------------------------------------------------------------------- locations

TEST_CASE("A location is added at the end after the save, with one row inserted", "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy inserted(rig.m_Settings.locations(), &QAbstractItemModel::rowsInserted);

    CHECK(rig.m_Settings.addLocation("  East Site ").isEmpty());
    CHECK(rig.m_Settings.locations()->rowCount() == 2);
    CHECK(inserted.count() == 0);
    rig.commit();

    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"North Site", "South Site", "East Site"});
    CHECK(inserted.count() == 1);
    CHECK(rig.m_Settings.configuration().m_Locations.back().m_SortOrder == 3);
    CHECK(rig.m_Settings.configuration().m_Locations.back().m_Name == "East Site");
}

TEST_CASE("A location name must be 1 to 200 characters and differ, ignoring case, from the others",
          "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK_FALSE(rig.m_Settings.addLocation("").isEmpty());
    CHECK_FALSE(rig.m_Settings.addLocation("   ").isEmpty());
    CHECK_FALSE(rig.m_Settings.addLocation(QString(201, 'x')).isEmpty());
    CHECK_FALSE(rig.m_Settings.addLocation("north SITE").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    CHECK(rig.m_Settings.addLocation(QString(200, 'x')).isEmpty());
    rig.commit();
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("A location is renamed; a name in use or a missing location is refused", "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_sample();
    const QString north = id_of(rig.m_Settings.configuration().m_Locations[0]);
    QSignalSpy changed(rig.m_Settings.locations(), &QAbstractItemModel::dataChanged);

    CHECK_FALSE(rig.m_Settings.renameLocation(north, "South Site").isEmpty());
    CHECK_FALSE(rig.m_Settings.renameLocation(north, "").isEmpty());
    CHECK_FALSE(rig.m_Settings.renameLocation("not an id", "East Site").isEmpty());
    CHECK_FALSE(rig.m_Settings.renameLocation("00000000-0000-4000-8000-0000000000ff", "East Site").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    // A change of case is a rename of the same location, not a clash with itself.
    CHECK(rig.m_Settings.renameLocation(north, "NORTH SITE").isEmpty());
    rig.commit();
    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"NORTH SITE", "South Site"});
    CHECK(changed.count() == 1);
}

TEST_CASE("A location moves in the list and the order is saved", "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_configuration(with_locations(4));
    QSignalSpy moved(rig.m_Settings.locations(), &QAbstractItemModel::rowsMoved);
    QSignalSpy reset(rig.m_Settings.locations(), &QAbstractItemModel::modelReset);

    CHECK(rig.m_Settings.moveLocation(0, 2).isEmpty());
    rig.commit();
    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"Site 1", "Site 2", "Site 0", "Site 3"});
    CHECK(moved.count() == 1);

    CHECK(rig.m_Settings.moveLocation(3, 0).isEmpty());
    rig.commit();
    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"Site 3", "Site 1", "Site 2", "Site 0"});
    CHECK(moved.count() == 2);
    CHECK(reset.count() == 0);

    // The file holds the order as the sort order of the locations.
    const auto read_back = import_configuration(*rig.store().saved());
    REQUIRE(read_back.has_value());
    std::vector<std::string> by_order(read_back->m_Locations.size());
    for (const Location& location : read_back->m_Locations)
    {
        by_order.at(static_cast<std::size_t>(location.m_SortOrder - 1)) = location.m_Name;
    }
    CHECK(by_order == std::vector<std::string>{"Site 3", "Site 1", "Site 2", "Site 0"});
}

TEST_CASE("A move to the same place saves nothing and a move out of the list is refused",
          "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_configuration(with_locations(3));

    CHECK(rig.m_Settings.moveLocation(1, 1).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK_FALSE(rig.m_Settings.moveLocation(-1, 0).isEmpty());
    CHECK_FALSE(rig.m_Settings.moveLocation(0, 3).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("Removing a location retires it: gone from the list, still in the configuration",
          "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_sample();
    const QString north = id_of(rig.m_Settings.configuration().m_Locations[0]);
    QSignalSpy removed(rig.m_Settings.locations(), &QAbstractItemModel::rowsRemoved);

    CHECK(rig.m_Settings.removeLocation(north).isEmpty());
    rig.commit();

    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"South Site"});
    CHECK(removed.count() == 1);
    REQUIRE(rig.m_Settings.configuration().m_Locations.size() == 2);
    CHECK(rig.m_Settings.configuration().m_Locations[0].m_Retired);
    CHECK(rig.m_Settings.configuration().m_Locations[0].m_Name == "North Site");

    // The same location cannot be removed twice, and a retired name can be used again.
    CHECK_FALSE(rig.m_Settings.removeLocation(north).isEmpty());
    CHECK_FALSE(rig.m_Settings.renameLocation(north, "Old Site").isEmpty());
    CHECK(rig.m_Settings.addLocation("North Site").isEmpty());
    rig.commit();
    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"South Site", "North Site"});
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("A move counts only the locations in the list, past a retired one", "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_configuration(with_locations(4));
    const QString second = id_of(rig.m_Settings.configuration().m_Locations[1]);
    REQUIRE(rig.m_Settings.removeLocation(second).isEmpty());
    rig.commit();
    REQUIRE(names_in(*rig.m_Settings.locations()) == QStringList{"Site 0", "Site 2", "Site 3"});

    CHECK(rig.m_Settings.moveLocation(0, 2).isEmpty());
    rig.commit();

    CHECK(names_in(*rig.m_Settings.locations()) == QStringList{"Site 2", "Site 3", "Site 0"});
}

TEST_CASE("A list stops at 500 locations", "[viewmodel][settings][locations]")
{
    SettingsRig rig;
    rig.load_configuration(with_locations(500));

    CHECK_FALSE(rig.m_Settings.addLocation("One too many").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

// ----------------------------------------------------------------------------------------------------- error types

TEST_CASE("A new error type goes to the end of the master list, off the board", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy inserted(rig.m_Settings.error_types(), &QAbstractItemModel::rowsInserted);

    CHECK(rig.m_Settings.addErrorType("Late Greeting").isEmpty());
    rig.commit();

    const QAbstractItemModel& types = *rig.m_Settings.error_types();
    REQUIRE(types.rowCount() == 12);
    CHECK(inserted.count() == 1);
    CHECK(text_at(types, 11, role_named(types, "name")) == "Late Greeting");
    CHECK_FALSE(types.data(types.index(11, 0), role_named(types, "onBoard")).toBool());
    CHECK(number_at(types, 11, role_named(types, "boardPosition")) == 0);
    // Not on the board: the layout is as it was.
    CHECK(rig.m_Settings.board_layout()->rowCount() == 12);
    CHECK_FALSE(labels_in(*rig.m_Settings.board_layout()).contains("Late Greeting"));
}

TEST_CASE("A new error type fills the first empty board position in the same save", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    Configuration configuration = with_error_types(6);
    // Positions 1, 2, 4, 5, 6 and 7 are taken: 3 is the first empty one.
    configuration.m_ErrorTypes[2].m_BoardPosition = 7;
    rig.load_configuration(configuration);
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);
    REQUIRE(rig.m_Settings.first_empty_position() == 3);

    CHECK(rig.m_Settings.addErrorType("  Late Greeting ").isEmpty());
    // Nothing is in force, or told, until the save has finished.
    CHECK(added.count() == 0);
    CHECK(rig.m_Settings.configuration().m_ErrorTypes.size() == 6);
    rig.commit();

    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    CHECK(text_at(board, 2, role_named(board, "label")) == "Late Greeting");
    CHECK(rig.m_Settings.configuration().m_ErrorTypes.back().m_BoardPosition == 3);
    // One save holds both the type and its place.
    REQUIRE(rig.store().saves().size() == 1);
    const auto read_back = import_configuration(rig.store().saves().front());
    REQUIRE(read_back.has_value());
    CHECK(read_back->m_ErrorTypes.back().m_Name == "Late Greeting");
    CHECK(read_back->m_ErrorTypes.back().m_BoardPosition == 3);
    REQUIRE(added.count() == 1);
    CHECK(added.at(0).at(0).toString() == "Late Greeting");
    CHECK(added.at(0).at(1).toInt() == 3);
    CHECK(rig.m_Settings.first_empty_position() == 8);
}

TEST_CASE("On a full board a new error type stays off the board and says so", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);
    CHECK(rig.m_Settings.first_empty_position() == 0);

    CHECK(rig.m_Settings.addErrorType("Late Greeting").isEmpty());
    rig.commit();

    CHECK_FALSE(rig.m_Settings.configuration().m_ErrorTypes.back().m_BoardPosition.has_value());
    REQUIRE(added.count() == 1);
    CHECK(added.at(0).at(1).toInt() == 0);
}

TEST_CASE("A new error type can be put on a chosen position in one change", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(3));
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);
    QAbstractItemModel& board = *rig.m_Settings.board_layout();

    SECTION("an empty position")
    {
        CHECK(rig.m_Settings.addErrorTypeAt("Late Greeting", 9).isEmpty());
        rig.commit();
        CHECK(text_at(board, 8, role_named(board, "label")) == "Late Greeting");
        CHECK(board.data(board.index(3, 0), role_named(board, "isEmpty")).toBool());
        REQUIRE(added.count() == 1);
        CHECK(added.at(0).at(1).toInt() == 9);
    }

    SECTION("a position that has a type sends that type off the board")
    {
        CHECK(rig.m_Settings.addErrorTypeAt("Late Greeting", 2).isEmpty());
        rig.commit();
        CHECK(text_at(board, 1, role_named(board, "label")) == "Late Greeting");
        CHECK_FALSE(rig.m_Settings.configuration().m_ErrorTypes[1].m_BoardPosition.has_value());
        CHECK(rig.m_Settings.configuration().m_ErrorTypes[0].m_BoardPosition == 1);
        CHECK(rig.m_Settings.configuration().m_ErrorTypes[2].m_BoardPosition == 3);
    }

    CHECK(rig.store().saves().size() == 1);
}

TEST_CASE("A new error type on a position is refused off the board, and for a taken name",
          "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(3));
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);

    CHECK_FALSE(rig.m_Settings.addErrorTypeAt("Late Greeting", 0).isEmpty());
    CHECK_FALSE(rig.m_Settings.addErrorTypeAt("Late Greeting", 12).isEmpty());
    // The names are compared ignoring case.
    CHECK(rig.m_Settings.addErrorTypeAt("type 1", 5).startsWith("There is already an error type called"));
    CHECK(rig.m_Settings.addErrorTypeAt("  ", 5) == "Enter a name.");
    CHECK(rig.m_Gateway.pending() == 0);
    CHECK(added.count() == 0);
}

TEST_CASE("A new error type whose save fails is not placed and not announced", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(3));
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);
    QSignalSpy failed(&rig.m_Settings, &SettingsModel::writeFailed);
    rig.store().fail_next_save(make_error(StorageErrorKind::DISK_FULL, "full"));

    CHECK(rig.m_Settings.addErrorType("Late Greeting").isEmpty());
    rig.commit();

    CHECK(failed.count() == 1);
    CHECK(added.count() == 0);
    CHECK(rig.m_Settings.configuration().m_ErrorTypes.size() == 3);
    CHECK(rig.m_Settings.first_empty_position() == 4);
}

TEST_CASE("The Other sheet's new type stays off the board", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(3));
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);

    REQUIRE(rig.m_Settings.add_error_type("Late Greeting").has_value());
    rig.commit();

    CHECK_FALSE(rig.m_Settings.configuration().m_ErrorTypes.back().m_BoardPosition.has_value());
    CHECK(added.count() == 0);
}

TEST_CASE("In a setup in progress a new type is placed and announced at once", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load();
    QSignalSpy added(&rig.m_Settings, &SettingsModel::errorTypeAdded);

    CHECK(rig.m_Settings.addErrorType("Wrong Response").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
    REQUIRE(added.count() == 1);
    CHECK(added.at(0).at(1).toInt() == 1);
    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    CHECK(text_at(board, 0, role_named(board, "label")) == "Wrong Response");
}

TEST_CASE("The next colour slot follows the master list, retired types included", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(5));
    CHECK(rig.m_Settings.next_colour_slot() == 6);
    REQUIRE(rig.m_Settings.retireErrorType(id_of(rig.m_Settings.configuration().m_ErrorTypes[4])).isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.next_colour_slot() == 6);
    // The retired type left its position, which is the first empty one now.
    CHECK(rig.m_Settings.first_empty_position() == 5);
}

TEST_CASE("The twelfth and thirteenth types reuse colour slots 1 and 2", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_sample();

    REQUIRE(rig.m_Settings.addErrorType("Late Greeting").isEmpty());
    rig.commit();
    REQUIRE(rig.m_Settings.addErrorType("Echo Delay").isEmpty());
    rig.commit();

    const QAbstractItemModel& types = *rig.m_Settings.error_types();
    const int slot = role_named(types, "colourSlot");
    for (int row = 0; row < 11; ++row)
    {
        CHECK(number_at(types, row, slot) == row + 1);
    }
    CHECK(number_at(types, 11, slot) == 1);
    CHECK(number_at(types, 12, slot) == 2);
}

TEST_CASE("An error type is renamed; a name in use is refused; a retired name may be reused",
          "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_sample();
    const auto& types = rig.m_Settings.configuration().m_ErrorTypes;
    const QString first = id_of(types[0]);
    const QString second = id_of(types[1]);

    CHECK_FALSE(rig.m_Settings.renameErrorType(first, "sequence out of order").isEmpty());
    CHECK_FALSE(rig.m_Settings.renameErrorType(first, " ").isEmpty());
    CHECK_FALSE(rig.m_Settings.renameErrorType("nonsense", "Anything").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    CHECK(rig.m_Settings.renameErrorType(first, "Wrong Answer").isEmpty());
    rig.commit();
    CHECK(text_at(*rig.m_Settings.error_types(), 0, role_named(*rig.m_Settings.error_types(), "name")) ==
          "Wrong Answer");

    REQUIRE(rig.m_Settings.retireErrorType(second).isEmpty());
    rig.commit();
    CHECK(rig.m_Settings.renameErrorType(first, "Sequence Out of Order").isEmpty());
    rig.commit();
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("Retiring an error type removes it from the list and the board but keeps it in the file",
          "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_sample();
    const auto& types = rig.m_Settings.configuration().m_ErrorTypes;
    const QString third = id_of(types[2]);
    const QString fourth = id_of(types[3]);
    const int fourth_slot = slot_of(*rig.m_Settings.error_types(), fourth);
    QSignalSpy removed(rig.m_Settings.error_types(), &QAbstractItemModel::rowsRemoved);
    QSignalSpy board_changed(rig.m_Settings.board_layout(), &QAbstractItemModel::dataChanged);

    CHECK(rig.m_Settings.retireErrorType(third).isEmpty());
    rig.commit();

    CHECK(removed.count() == 1);
    CHECK(rig.m_Settings.error_types()->rowCount() == 10);
    CHECK(slot_of(*rig.m_Settings.error_types(), third) == -1);
    // The others keep their colours: the retired type keeps its place in the master list.
    CHECK(slot_of(*rig.m_Settings.error_types(), fourth) == fourth_slot);
    // Its tile is empty and the rest of the board is as it was.
    REQUIRE(board_changed.count() == 1);
    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    CHECK(board.data(board.index(2, 0), role_named(board, "isEmpty")).toBool());
    CHECK(text_at(board, 3, role_named(board, "label")) == "Track Not Found");
    // The file still has it, retired, so past entries still name it.
    const auto read_back = import_configuration(*rig.store().saved());
    REQUIRE(read_back.has_value());
    REQUIRE(read_back->m_ErrorTypes.size() == 11);
    CHECK(read_back->m_ErrorTypes[2].m_Retired);
    CHECK(read_back->m_ErrorTypes[2].m_Name == "Wrong Track Played");

    CHECK_FALSE(rig.m_Settings.retireErrorType(third).isEmpty());
    CHECK_FALSE(rig.m_Settings.renameErrorType(third, "Something Else").isEmpty());
    // A new type after a retired one still takes the next index of the master list.
    REQUIRE(rig.m_Settings.addErrorType("Late Greeting").isEmpty());
    rig.commit();
    const QAbstractItemModel& updated = *rig.m_Settings.error_types();
    CHECK(number_at(updated, updated.rowCount() - 1, role_named(updated, "colourSlot")) == 1);
}

TEST_CASE("A list stops at 500 error types", "[viewmodel][settings][errors]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(500));

    CHECK_FALSE(rig.m_Settings.addErrorType("One too many").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("The colour of a type is its index in the master list modulo 11, plus one", "[viewmodel][settings][errors]")
{
    CHECK(viewmodel::tile_colour_slot(0) == 1);
    CHECK(viewmodel::tile_colour_slot(10) == 11);
    CHECK(viewmodel::tile_colour_slot(11) == 1);
    CHECK(viewmodel::tile_colour_slot(12) == 2);
    CHECK(viewmodel::tile_colour_slot(22) == 1);

    // The list models report the same rule.
    SettingsRig rig;
    rig.load_configuration(with_error_types(13));
    const QAbstractItemModel& types = *rig.m_Settings.error_types();
    for (int row = 0; row < types.rowCount(); ++row)
    {
        CHECK(number_at(types, row, role_named(types, "colourSlot")) ==
              viewmodel::tile_colour_slot(static_cast<std::size_t>(row)));
    }
}

// ------------------------------------------------------------------------------------------------------ the board

TEST_CASE("The board layout is 12 tiles: eleven positions and Other", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_sample();
    QAbstractItemModel& board = *rig.m_Settings.board_layout();

    REQUIRE(board.rowCount() == 12);
    CHECK(text_at(board, 0, role_named(board, "label")) == "Wrong Response");
    CHECK(text_at(board, 0, role_named(board, "typeId")) == "00000000-0000-4000-8000-0000000000a1");
    CHECK(number_at(board, 0, role_named(board, "colourSlot")) == 1);
    CHECK(number_at(board, 0, role_named(board, "position")) == 1);
    CHECK(text_at(board, 10, role_named(board, "label")) == "Navigation Failure");
    CHECK(number_at(board, 10, role_named(board, "colourSlot")) == 11);
    CHECK(text_at(board, 0, role_named(board, "accessibleName")) == "Wrong Response");
    for (int row = 0; row < 11; ++row)
    {
        CHECK_FALSE(board.data(board.index(row, 0), role_named(board, "isOther")).toBool());
        CHECK_FALSE(board.data(board.index(row, 0), role_named(board, "isEmpty")).toBool());
    }
    CHECK(board.data(board.index(11, 0), role_named(board, "isOther")).toBool());
    CHECK(text_at(board, 11, role_named(board, "typeId")).isEmpty());
    CHECK(number_at(board, 11, role_named(board, "colourSlot")) == 0);
    CHECK(number_at(board, 11, role_named(board, "position")) == 12);
}

TEST_CASE("A configuration with nine types has two empty tiles", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(9));
    QAbstractItemModel& board = *rig.m_Settings.board_layout();

    REQUIRE(board.rowCount() == 12);
    for (int row = 0; row < 9; ++row)
    {
        CHECK_FALSE(board.data(board.index(row, 0), role_named(board, "isEmpty")).toBool());
    }
    for (int row = 9; row < 11; ++row)
    {
        CHECK(board.data(board.index(row, 0), role_named(board, "isEmpty")).toBool());
        CHECK(text_at(board, row, role_named(board, "label")).isEmpty());
        CHECK(number_at(board, row, role_named(board, "colourSlot")) == 0);
    }
    CHECK(text_at(board, 9, role_named(board, "accessibleName")).contains("10"));
}

TEST_CASE("A drag moves the tile; the tiles between shift; colours stay with the types", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_sample();
    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    const QAbstractItemModel& types = *rig.m_Settings.error_types();
    const QString first = id_of(rig.m_Settings.configuration().m_ErrorTypes[0]);
    const int first_slot = slot_of(types, first);
    QSignalSpy changed(&board, &QAbstractItemModel::dataChanged);

    CHECK(rig.m_Settings.moveBoardTile(1, 3).isEmpty());
    CHECK(changed.count() == 0);
    rig.commit();

    CHECK(labels_in(board).mid(0, 4) ==
          QStringList{"Sequence Out of Order", "Wrong Track Played", "Wrong Response", "Track Not Found"});
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
    CHECK(changed.at(0).at(1).value<QModelIndex>().row() == 2);
    CHECK(rig.store().saves().size() == 1);
    // The type that moved keeps its colour, on the tile and in the master list.
    CHECK(number_at(board, 2, role_named(board, "colourSlot")) == first_slot);
    CHECK(slot_of(types, first) == first_slot);
    CHECK(number_at(types, 0, role_named(types, "boardPosition")) == 3);
    CHECK(text_at(types, 0, role_named(types, "name")) == "Wrong Response");

    // And back.
    CHECK(rig.m_Settings.moveBoardTile(3, 1).isEmpty());
    rig.commit();
    CHECK(labels_in(board).mid(0, 3) == QStringList{"Wrong Response", "Sequence Out of Order", "Wrong Track Played"});
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("A tile can be dragged onto an empty position", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(9));
    QAbstractItemModel& board = *rig.m_Settings.board_layout();

    CHECK(rig.m_Settings.moveBoardTile(1, 11).isEmpty());
    rig.commit();

    CHECK(text_at(board, 10, role_named(board, "label")) == "Type 0");
    CHECK(text_at(board, 0, role_named(board, "label")) == "Type 1");
}

TEST_CASE("A drag from an empty tile, or to a place off the board, is refused", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(9));

    CHECK_FALSE(rig.m_Settings.moveBoardTile(10, 1).isEmpty());
    CHECK_FALSE(rig.m_Settings.moveBoardTile(0, 1).isEmpty());
    CHECK_FALSE(rig.m_Settings.moveBoardTile(1, 12).isEmpty());
    CHECK_FALSE(rig.m_Settings.moveBoardTile(12, 1).isEmpty());
    CHECK(rig.m_Settings.moveBoardTile(2, 2).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("Assigning a type that is on the board elsewhere swaps the two tiles", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_sample();
    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    const QString fifth = id_of(rig.m_Settings.configuration().m_ErrorTypes[4]);
    QSignalSpy changed(&board, &QAbstractItemModel::dataChanged);

    CHECK(rig.m_Settings.assignBoardTile(1, fifth).isEmpty());
    rig.commit();

    CHECK(text_at(board, 0, role_named(board, "label")) == "Track Skipped");
    CHECK(text_at(board, 4, role_named(board, "label")) == "Wrong Response");
    CHECK(text_at(board, 1, role_named(board, "label")) == "Sequence Out of Order");
    REQUIRE(changed.count() == 1);
    CHECK(changed.at(0).at(0).value<QModelIndex>().row() == 0);
    CHECK(changed.at(0).at(1).value<QModelIndex>().row() == 4);
    CHECK(rig.store().saves().size() == 1);
    // Every type is still on the board exactly once.
    int on_board = 0;
    for (const ErrorType& type : rig.m_Settings.configuration().m_ErrorTypes)
    {
        on_board += type.m_BoardPosition.has_value() ? 1 : 0;
    }
    CHECK(on_board == 11);
}

TEST_CASE("Assigning a type that is off the board sends the tile's type off", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_sample();
    const auto added = rig.m_Settings.add_error_type("Late Greeting");
    REQUIRE(added.has_value());
    rig.commit();
    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    const QAbstractItemModel& types = *rig.m_Settings.error_types();
    const QString replaced = id_of(rig.m_Settings.configuration().m_ErrorTypes[1]);

    CHECK(rig.m_Settings.assignBoardTile(2, QString::fromStdString(added->format())).isEmpty());
    rig.commit();

    CHECK(text_at(board, 1, role_named(board, "label")) == "Late Greeting");
    CHECK(number_at(board, 1, role_named(board, "colourSlot")) == 1); // its index in the master list is 11
    int row_of_replaced = -1;
    for (int row = 0; row < types.rowCount(); ++row)
    {
        if (text_at(types, row, role_named(types, "typeId")) == replaced)
        {
            row_of_replaced = row;
        }
    }
    REQUIRE(row_of_replaced >= 0);
    CHECK_FALSE(types.data(types.index(row_of_replaced, 0), role_named(types, "onBoard")).toBool());
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("A type can be assigned to an empty tile", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_configuration(with_error_types(9));
    REQUIRE(rig.m_Settings.addErrorType("Late Greeting").isEmpty());
    rig.commit();
    const QString added = id_of(rig.m_Settings.configuration().m_ErrorTypes.back());
    QAbstractItemModel& board = *rig.m_Settings.board_layout();

    CHECK(rig.m_Settings.assignBoardTile(11, added).isEmpty());
    rig.commit();

    CHECK(text_at(board, 10, role_named(board, "label")) == "Late Greeting");
    CHECK(board.data(board.index(9, 0), role_named(board, "isEmpty")).toBool());
}

TEST_CASE("Assigning refuses a retired or unknown type and a position off the board", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_sample();
    const QString first = id_of(rig.m_Settings.configuration().m_ErrorTypes[0]);
    const QString second = id_of(rig.m_Settings.configuration().m_ErrorTypes[1]);
    REQUIRE(rig.m_Settings.retireErrorType(second).isEmpty());
    rig.commit();

    CHECK_FALSE(rig.m_Settings.assignBoardTile(3, second).isEmpty());
    CHECK_FALSE(rig.m_Settings.assignBoardTile(3, "00000000-0000-4000-8000-0000000000ff").isEmpty());
    CHECK_FALSE(rig.m_Settings.assignBoardTile(3, "").isEmpty());
    CHECK_FALSE(rig.m_Settings.assignBoardTile(0, first).isEmpty());
    CHECK_FALSE(rig.m_Settings.assignBoardTile(12, first).isEmpty());
    // The type already on the tile: nothing to do.
    CHECK(rig.m_Settings.assignBoardTile(1, first).isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("The board layout never resets or changes its rows, whatever changes", "[viewmodel][settings][board]")
{
    SettingsRig rig;
    rig.load_sample();
    QAbstractItemModel& board = *rig.m_Settings.board_layout();
    QSignalSpy reset(&board, &QAbstractItemModel::modelReset);
    QSignalSpy layout(&board, &QAbstractItemModel::layoutChanged);
    QSignalSpy inserted(&board, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&board, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(&board, &QAbstractItemModel::rowsMoved);

    REQUIRE(rig.m_Settings.moveBoardTile(1, 5).isEmpty());
    rig.commit();
    REQUIRE(rig.m_Settings.retireErrorType(id_of(rig.m_Settings.configuration().m_ErrorTypes[6])).isEmpty());
    rig.commit();
    rig.store().add_file("/f.json", export_configuration(scenario_configuration()));
    rig.m_Settings.loadImport("/f.json");
    rig.m_Gateway.run_all();
    rig.m_Settings.applyImport();
    rig.commit();

    CHECK(board.rowCount() == 12);
    CHECK(reset.count() == 0);
    CHECK(layout.count() == 0);
    CHECK(inserted.count() == 0);
    CHECK(removed.count() == 0);
    CHECK(moved.count() == 0);
}

// ---------------------------------------------------------------------------------------------------------- templates

TEST_CASE("A note template is added with its name, prompt and pattern", "[viewmodel][settings][templates]")
{
    SettingsRig rig;
    rig.load_sample();
    QSignalSpy inserted(rig.m_Settings.templates(), &QAbstractItemModel::rowsInserted);

    CHECK(rig.m_Settings.addTemplate("Requested song", "Which song?", "Song {value} requested twice").isEmpty());
    CHECK(rig.m_Settings.templates()->rowCount() == 2);
    rig.commit();

    const QAbstractItemModel& templates = *rig.m_Settings.templates();
    REQUIRE(templates.rowCount() == 3);
    CHECK(inserted.count() == 1);
    CHECK(text_at(templates, 2, role_named(templates, "name")) == "Requested song");
    CHECK(text_at(templates, 2, role_named(templates, "valuePrompt")) == "Which song?");
    CHECK(text_at(templates, 2, role_named(templates, "outputPattern")) == "Song {value} requested twice");
    CHECK(text_at(templates, 2, role_named(templates, "templateId")).size() == 36);
    CHECK(rig.m_Settings.configuration().m_Templates.back().m_SortOrder == 3);
    CHECK(import_configuration(*rig.store().saved()).has_value());
}

TEST_CASE("A pattern needs {value}; {name} and stray braces are refused with a message",
          "[viewmodel][settings][templates]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK(rig.m_Settings.validatePattern("Played {value}").isEmpty());
    CHECK(rig.m_Settings.validatePattern("{value} and {value}").isEmpty());
    CHECK(rig.m_Settings.validatePattern("Braces {{ and }} with {value}").isEmpty());
    CHECK(rig.m_Settings.validatePattern(QString("{value}") + QString(493, 'x')).isEmpty());

    CHECK(rig.m_Settings.validatePattern("Played song").contains("{value}"));
    CHECK(rig.m_Settings.validatePattern("").contains("{value}"));
    CHECK(rig.m_Settings.validatePattern("Played {name}").contains("{name}"));
    CHECK(rig.m_Settings.validatePattern("Played {value} {name}").contains("{name}"));
    CHECK(rig.m_Settings.validatePattern("Played {value").contains("brace"));
    CHECK(rig.m_Settings.validatePattern("Played } {value}").contains("brace"));
    CHECK_FALSE(rig.m_Settings.validatePattern(QString("{value}") + QString(494, 'x')).isEmpty());
}

TEST_CASE("A template with a bad name, prompt or pattern is refused and nothing is saved",
          "[viewmodel][settings][templates]")
{
    SettingsRig rig;
    rig.load_sample();

    CHECK_FALSE(rig.m_Settings.addTemplate("", "Which song?", "Played {value}").isEmpty());
    CHECK_FALSE(rig.m_Settings.addTemplate("Name", "", "Played {value}").isEmpty());
    CHECK_FALSE(rig.m_Settings.addTemplate("Name", "Which song?", "Played").isEmpty());
    CHECK_FALSE(rig.m_Settings.addTemplate("Name", "Which song?", "Played {name}").isEmpty());
    CHECK_FALSE(rig.m_Settings.addTemplate("played SONG", "Which song?", "Played {value}").isEmpty());
    CHECK_FALSE(rig.m_Settings.addTemplate(QString(201, 'x'), "Which song?", "Played {value}").isEmpty());
    CHECK_FALSE(rig.m_Settings.addTemplate("Name", QString(201, 'x'), "Played {value}").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("A template is renamed and edited; a name in use is refused", "[viewmodel][settings][templates]")
{
    SettingsRig rig;
    rig.load_sample();
    const QString played = id_of(rig.m_Settings.configuration().m_Templates[0]);
    const QAbstractItemModel& templates = *rig.m_Settings.templates();

    CHECK_FALSE(rig.m_Settings.renameTemplate(played, "visitor interaction").isEmpty());
    CHECK_FALSE(rig.m_Settings.renameTemplate("nonsense", "Whatever").isEmpty());
    CHECK_FALSE(rig.m_Settings.updateTemplate(played, "Played song", "Which song?", "No placeholder").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);

    CHECK(rig.m_Settings.renameTemplate(played, "Song played").isEmpty());
    rig.commit();
    CHECK(text_at(templates, 0, role_named(templates, "name")) == "Song played");

    CHECK(rig.m_Settings.updateTemplate(played, "Song played", "Which track?", "Track {value} played").isEmpty());
    rig.commit();
    CHECK(text_at(templates, 0, role_named(templates, "valuePrompt")) == "Which track?");
    CHECK(text_at(templates, 0, role_named(templates, "outputPattern")) == "Track {value} played");
    CHECK(rig.store().saves().size() == 2);

    // No change, no save.
    CHECK(rig.m_Settings.updateTemplate(played, "Song played", "Which track?", "Track {value} played").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}

TEST_CASE("A list stops at 500 templates", "[viewmodel][settings][templates]")
{
    Configuration configuration = sample_configuration();
    configuration.m_Templates.clear();
    for (int i = 0; i < 500; ++i)
    {
        NoteTemplate note_template;
        note_template.m_Id = test_id<NoteTemplateId>(7000 + static_cast<std::uint64_t>(i));
        note_template.m_Name = "Template " + std::to_string(i);
        note_template.m_ValuePrompt = "Which?";
        note_template.m_OutputPattern = "{value}";
        note_template.m_SortOrder = i + 1;
        configuration.m_Templates.push_back(std::move(note_template));
    }
    SettingsRig rig;
    rig.load_configuration(configuration);

    CHECK_FALSE(rig.m_Settings.addTemplate("One too many", "Which?", "{value}").isEmpty());
    CHECK(rig.m_Gateway.pending() == 0);
}
