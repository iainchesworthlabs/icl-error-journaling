// SPDX-License-Identifier: GPL-3.0-or-later
//
// The signals of the list models of work package 3.3: the least a view needs to hear for each kind of change. Unit
// tests that feed the models configurations directly.
#include "error_journaling/configuration.hpp"
#include "settings_rig.hpp"
#include "viewmodel/board_layout_model.hpp"
#include "viewmodel/error_type_list_model.hpp"
#include "viewmodel/location_list_model.hpp"
#include "viewmodel/template_list_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSignalSpy>
#include <QStringList>
#include <string>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::viewmodel;

namespace
{

Configuration locations(std::initializer_list<const char*> names)
{
    Configuration configuration;
    int order = 1;
    for (const char* name : names)
    {
        Location location;
        // The id follows the name, so the same name is the same location in two configurations.
        location.m_Id =
            test_id<LocationId>(100 + static_cast<std::uint64_t>(name[0]) * 10 + static_cast<std::uint64_t>(name[1]));
        location.m_Name = name;
        location.m_SortOrder = order++;
        configuration.m_Locations.push_back(std::move(location));
    }
    return configuration;
}

QStringList names(const LocationListModel& model)
{
    QStringList result;
    for (int row = 0; row < model.rowCount(); ++row)
    {
        result.push_back(model.data(model.index(row), LocationListModel::NAME).toString());
    }
    return result;
}

struct Spies
{
    explicit Spies(QAbstractItemModel& model)
        : m_Changed(&model, &QAbstractItemModel::dataChanged), m_Inserted(&model, &QAbstractItemModel::rowsInserted),
          m_Removed(&model, &QAbstractItemModel::rowsRemoved), m_Moved(&model, &QAbstractItemModel::rowsMoved),
          m_Reset(&model, &QAbstractItemModel::modelReset)
    {
    }

    [[nodiscard]] int total() const
    {
        return static_cast<int>(m_Changed.count() + m_Inserted.count() + m_Removed.count() + m_Moved.count() +
                                m_Reset.count());
    }

    QSignalSpy m_Changed;
    QSignalSpy m_Inserted;
    QSignalSpy m_Removed;
    QSignalSpy m_Moved;
    QSignalSpy m_Reset;
};

} // namespace

TEST_CASE("A list model tells nothing when nothing changed", "[viewmodel][settings][models]")
{
    LocationListModel model;
    model.refresh(locations({"AA", "BB", "CC"}));
    Spies spies(model);

    model.refresh(locations({"AA", "BB", "CC"}));

    CHECK(spies.total() == 0);
}

TEST_CASE("A list model reports rows added at the end as one insertion", "[viewmodel][settings][models]")
{
    LocationListModel model;
    model.refresh(locations({"AA"}));
    Spies spies(model);

    model.refresh(locations({"AA", "BB", "CC"}));

    CHECK(spies.total() == 1);
    REQUIRE(spies.m_Inserted.count() == 1);
    CHECK(spies.m_Inserted.at(0).at(1).toInt() == 1);
    CHECK(spies.m_Inserted.at(0).at(2).toInt() == 2);
    CHECK(names(model) == QStringList{"AA", "BB", "CC"});
}

TEST_CASE("A list model reports each run of removed rows once", "[viewmodel][settings][models]")
{
    LocationListModel model;
    model.refresh(locations({"AA", "BB", "CC", "DD", "EE", "FF"}));
    Spies spies(model);

    model.refresh(locations({"AA", "DD", "FF"}));

    CHECK(spies.m_Removed.count() == 2);
    CHECK(spies.total() == 2);
    CHECK(names(model) == QStringList{"AA", "DD", "FF"});
}

TEST_CASE("A list model reports one row moved up or down as one move", "[viewmodel][settings][models]")
{
    LocationListModel model;
    model.refresh(locations({"AA", "BB", "CC", "DD"}));
    Spies spies(model);

    Configuration down = locations({"BB", "CC", "AA", "DD"});
    model.refresh(down);
    CHECK(spies.m_Moved.count() == 1);
    CHECK(names(model) == QStringList{"BB", "CC", "AA", "DD"});

    model.refresh(locations({"BB", "DD", "CC", "AA"}));
    CHECK(spies.m_Moved.count() == 2);
    CHECK(names(model) == QStringList{"BB", "DD", "CC", "AA"});

    // Two neighbours swapping places is one move too.
    model.refresh(locations({"DD", "BB", "CC", "AA"}));
    CHECK(spies.m_Moved.count() == 3);
    CHECK(names(model) == QStringList{"DD", "BB", "CC", "AA"});
    CHECK(spies.total() == 3);
}

TEST_CASE("A list model resets only for a change it cannot describe smaller", "[viewmodel][settings][models]")
{
    LocationListModel model;
    model.refresh(locations({"AA", "BB", "CC"}));
    Spies spies(model);

    model.refresh(locations({"CC", "AA", "BB", "DD"}));

    CHECK(spies.m_Reset.count() == 1);
    CHECK(spies.total() == 1);
    CHECK(names(model) == QStringList{"CC", "AA", "BB", "DD"});
}

TEST_CASE("A list model reports a changed name as one dataChanged over the rows that differ",
          "[viewmodel][settings][models]")
{
    LocationListModel model;
    Configuration configuration = locations({"AA", "BB", "CC", "DD"});
    model.refresh(configuration);
    Spies spies(model);

    configuration.m_Locations[1].m_Name = "bb renamed";
    configuration.m_Locations[2].m_Name = "cc renamed";
    model.refresh(configuration);

    CHECK(spies.total() == 1);
    REQUIRE(spies.m_Changed.count() == 1);
    CHECK(spies.m_Changed.at(0).at(0).value<QModelIndex>().row() == 1);
    CHECK(spies.m_Changed.at(0).at(1).value<QModelIndex>().row() == 2);
}

TEST_CASE("A model answers for rows and roles it does not have with nothing", "[viewmodel][settings][models]")
{
    LocationListModel model;
    model.refresh(locations({"AA"}));

    CHECK_FALSE(model.data(model.index(5), LocationListModel::NAME).isValid());
    CHECK_FALSE(model.data({}, LocationListModel::NAME).isValid());
    CHECK_FALSE(model.data(model.index(0), Qt::UserRole + 99).isValid());
    CHECK(model.rowCount(model.index(0)) == 0);
}

TEST_CASE("The location list orders by sort order and leaves retired locations out", "[viewmodel][settings][models]")
{
    LocationListModel model;
    Configuration configuration = locations({"AA", "BB", "CC"});
    configuration.m_Locations[0].m_SortOrder = 30;
    configuration.m_Locations[1].m_SortOrder = 10;
    configuration.m_Locations[2].m_SortOrder = 20;
    configuration.m_Locations[2].m_Retired = true;

    model.refresh(configuration);

    CHECK(names(model) == QStringList{"BB", "AA"});
    CHECK(model.roleNames().value(LocationListModel::LOCATION_ID) == "locationId");
}

TEST_CASE("The error type list keeps the colour of a type when others around it go", "[viewmodel][settings][models]")
{
    ErrorTypeListModel model;
    Configuration configuration = sample_configuration();
    model.refresh(configuration);
    Spies spies(model);

    configuration.m_ErrorTypes[0].m_Retired = true;
    configuration.m_ErrorTypes[5].m_Retired = true;
    model.refresh(configuration);

    CHECK(spies.m_Removed.count() == 2);
    REQUIRE(model.rowCount() == 9);
    CHECK(model.data(model.index(0), ErrorTypeListModel::COLOUR_SLOT).toInt() == 2); // the second type, still 2
    CHECK(model.data(model.index(3), ErrorTypeListModel::COLOUR_SLOT).toInt() == 5); // the fifth, still 5
    CHECK(model.data(model.index(4), ErrorTypeListModel::COLOUR_SLOT).toInt() == 7); // the seventh, still 7
    CHECK(model.data(model.index(0), ErrorTypeListModel::ON_BOARD).toBool());
    CHECK(model.roleNames().value(ErrorTypeListModel::COLOUR_SLOT) == "colourSlot");
}

TEST_CASE("The template list orders by sort order", "[viewmodel][settings][models]")
{
    TemplateListModel model;
    Configuration configuration = sample_configuration();
    configuration.m_Templates[0].m_SortOrder = 9;

    model.refresh(configuration);

    REQUIRE(model.rowCount() == 2);
    CHECK(model.data(model.index(0), TemplateListModel::NAME).toString() == "Visitor interaction");
    CHECK(model.data(model.index(1), TemplateListModel::OUTPUT_PATTERN).toString() == "Played {value}");
}

TEST_CASE("The board layout puts a type on the tile of its position and never retired ones",
          "[viewmodel][settings][models]")
{
    BoardLayoutModel model;
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes[0].m_BoardPosition = 11; // swapped with the last
    configuration.m_ErrorTypes[10].m_BoardPosition = 1;
    configuration.m_ErrorTypes[4].m_Retired = true;

    model.refresh(configuration);

    CHECK(model.data(model.index(0), BoardLayoutModel::LABEL).toString() == "Navigation Failure");
    CHECK(model.data(model.index(0), BoardLayoutModel::COLOUR_SLOT).toInt() == 11);
    CHECK(model.data(model.index(10), BoardLayoutModel::COLOUR_SLOT).toInt() == 1);
    CHECK(model.data(model.index(4), BoardLayoutModel::IS_EMPTY).toBool());
    CHECK(model.data(model.index(11), BoardLayoutModel::IS_OTHER).toBool());
    CHECK(model.data(model.index(11), BoardLayoutModel::LABEL).toString() == "Other");
}

TEST_CASE("The board layout reports one dataChanged over the tiles that differ and nothing else",
          "[viewmodel][settings][models]")
{
    BoardLayoutModel model;
    Configuration configuration = sample_configuration();
    model.refresh(configuration);
    Spies spies(model);

    model.refresh(configuration);
    CHECK(spies.total() == 0);

    configuration.m_ErrorTypes[2].m_BoardPosition = 8;
    configuration.m_ErrorTypes[7].m_BoardPosition = 3;
    model.refresh(configuration);

    CHECK(spies.total() == 1);
    REQUIRE(spies.m_Changed.count() == 1);
    CHECK(spies.m_Changed.at(0).at(0).value<QModelIndex>().row() == 2);
    CHECK(spies.m_Changed.at(0).at(1).value<QModelIndex>().row() == 7);
    CHECK(model.rowCount() == 12);
}

TEST_CASE("The board layout of an empty configuration is twelve tiles of which eleven are empty",
          "[viewmodel][settings][models]")
{
    BoardLayoutModel model;
    model.refresh(Configuration{});

    CHECK(model.rowCount() == 12);
    int empty = 0;
    for (int row = 0; row < 12; ++row)
    {
        empty += model.data(model.index(row), BoardLayoutModel::IS_EMPTY).toBool() ? 1 : 0;
    }
    CHECK(empty == 11);
    CHECK(model.data(model.index(3), BoardLayoutModel::ACCESSIBLE_NAME).toString().contains("4"));
}
