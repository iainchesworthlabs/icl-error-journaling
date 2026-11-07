// SPDX-License-Identifier: GPL-3.0-or-later
// Locations and its rename sheet (docs/plan/stage-4.md, 4.13): editor.locations, editor.voiceover and the cross-cutting
// checks.
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

constexpr auto LOCATIONS = "locationRow";

QStringList names(World& world)
{
    return row_names(world, LOCATIONS);
}

/// Types `text` into the field of the new location and taps "Add location".
void add_location(World& world, const QString& text)
{
    enter_text(world, QStringLiteral("newLocationField"), text);
    tap(world, QStringLiteral("addButton"));
}

QStringList names_in_model(World& world)
{
    QStringList listed;
    const QAbstractItemModel* model = world.model().locations();
    for (int row = 0; row < model->rowCount(); ++row)
    {
        listed.push_back(model->data(model->index(row, 0), test::role_named(*model, "name")).toString());
    }
    return listed;
}

} // namespace

TEST_CASE("editor.locations: the locations are listed in order, with a field to add one", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    CHECK(names(world) == QStringList{QStringLiteral("North Site"), QStringLiteral("South Site")});
    CHECK(item(world, QStringLiteral("locationGroup"))->property("title").toString() ==
          QStringLiteral("Locations · 2"));
    CHECK(field_text(world, QStringLiteral("newLocationField")).isEmpty());
}

TEST_CASE("editor.locations: adding a location puts it last and clears the field", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    add_location(world, QStringLiteral("West Yard"));
    CHECK(names(world) ==
          QStringList{QStringLiteral("North Site"), QStringLiteral("South Site"), QStringLiteral("West Yard")});
    CHECK(field_text(world, QStringLiteral("newLocationField")).isEmpty());
    CHECK(field_error(world, QStringLiteral("newLocationField")).isEmpty());
}

TEST_CASE("editor.locations: an empty, repeated or over-long name is refused under the field", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    // Nothing typed.
    tap(world, QStringLiteral("addButton"));
    CHECK_FALSE(field_error(world, QStringLiteral("newLocationField")).isEmpty());
    CHECK(names(world).size() == 2);

    // A name that is there, in other capitals. The text stays so that it can be corrected, and the next edit clears the
    // message.
    add_location(world, QStringLiteral("NORTH site"));
    CHECK(field_error(world, QStringLiteral("newLocationField"))
              .contains(QStringLiteral("North Site"), Qt::CaseInsensitive));
    CHECK(field_text(world, QStringLiteral("newLocationField")) == QStringLiteral("NORTH site"));
    CHECK(names(world).size() == 2);
    enter_text(world, QStringLiteral("newLocationField"), QStringLiteral("North"));
    CHECK(field_error(world, QStringLiteral("newLocationField")).isEmpty());

    // 201 characters are refused and 200 are saved.
    add_location(world, QString(201, QLatin1Char('x')));
    CHECK_FALSE(field_error(world, QStringLiteral("newLocationField")).isEmpty());
    CHECK(names(world).size() == 2);
    add_location(world, QString(200, QLatin1Char('x')));
    CHECK(names(world).size() == 3);
}

TEST_CASE("editor.locations: a list of 500 refuses a 501st", "[ui][editor]")
{
    Configuration configuration = test::sample_configuration();
    configuration.m_Locations.clear();
    for (int i = 1; i <= 500; ++i)
    {
        Location location;
        location.m_Id = test::test_id<LocationId>(static_cast<std::uint64_t>(1000 + i));
        location.m_Name = "Place " + std::to_string(i);
        location.m_SortOrder = i;
        configuration.m_Locations.push_back(std::move(location));
    }
    World world(configuration);
    world.load("Locations");
    world.m_Ui.layout();
    CHECK(names(world).size() == 500);

    add_location(world, QStringLiteral("One more"));
    CHECK_FALSE(field_error(world, QStringLiteral("newLocationField")).isEmpty());
    CHECK(names(world).size() == 500);
}

TEST_CASE("editor.locations: a row opens a sheet to rename the location", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    world.m_Ui.tap(label_of(row_with(world, LOCATIONS, QStringLiteral("South Site"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(field_text(world, QStringLiteral("renameField")) == QStringLiteral("South Site"));

    // A repeated name is refused in the sheet, which stays.
    enter_text(world, QStringLiteral("renameField"), QStringLiteral("north site"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(field_error(world, QStringLiteral("renameField")).isEmpty());
    CHECK_FALSE(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());

    // A good one is saved in the same place.
    enter_text(world, QStringLiteral("renameField"), QStringLiteral("East Depot"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world) == QStringList{QStringLiteral("North Site"), QStringLiteral("East Depot")});

    // Cancel leaves the name.
    world.m_Ui.tap(label_of(row_with(world, LOCATIONS, QStringLiteral("East Depot"))));
    world.play(400);
    enter_text(world, QStringLiteral("renameField"), QStringLiteral("Somewhere"));
    tap_in_sheet(world, QStringLiteral("navLeading"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world) == QStringList{QStringLiteral("North Site"), QStringLiteral("East Depot")});
}

TEST_CASE("editor.locations: Remove retires the location, which leaves the list and the model keeps it", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    QQuickItem* row = row_with(world, LOCATIONS, QStringLiteral("North Site"));
    world.m_Ui.tap(find_item(row, QStringLiteral("editorRemove")));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("South Site")});
    CHECK(names_in_model(world) == QStringList{QStringLiteral("South Site")});

    int retired = 0;
    for (const Location& location : world.model().configuration().m_Locations)
    {
        retired += location.m_Retired ? 1 : 0;
    }
    CHECK(retired == 1);
}

TEST_CASE("editor.locations: the arrows reorder with 0-based rows, and the ends cannot move past the list",
          "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    QQuickItem* north = row_with(world, LOCATIONS, QStringLiteral("North Site"));
    CHECK_FALSE(child(north, QStringLiteral("editorMoveEarlier"))->isEnabled());
    world.m_Ui.tap(find_item(north, QStringLiteral("editorMoveLater")));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("South Site"), QStringLiteral("North Site")});
    CHECK(names_in_model(world) == names(world));

    QQuickItem* last = row_with(world, LOCATIONS, QStringLiteral("North Site"));
    CHECK_FALSE(child(last, QStringLiteral("editorMoveLater"))->isEnabled());
    world.m_Ui.tap(find_item(last, QStringLiteral("editorMoveEarlier")));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("North Site"), QStringLiteral("South Site")});
}

TEST_CASE("editor.locations: dragging a handle by whole rows moves the location", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();

    QQuickItem* north = row_with(world, LOCATIONS, QStringLiteral("North Site"));
    QQuickItem* handle = find_item(north, QStringLiteral("editorHandle"));
    REQUIRE(handle != nullptr);
    const double row_height = world.m_Ui.geometry(north).height();
    const QPointF from(handle->width() / 2, handle->height() / 2);
    // Down by more than the list is long: the move stops at the last place.
    world.m_Ui.drag(handle, from, from + QPointF(0, row_height * 3), 12);
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("South Site"), QStringLiteral("North Site")});

    // A short drag moves nothing.
    QQuickItem* south = row_with(world, LOCATIONS, QStringLiteral("South Site"));
    QQuickItem* south_handle = find_item(south, QStringLiteral("editorHandle"));
    world.m_Ui.drag(south_handle, from, from + QPointF(0, row_height / 4), 6);
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("South Site"), QStringLiteral("North Site")});
}

TEST_CASE("editor.locations: a refused change and a failed save are shown in a banner", "[ui][editor]")
{
    World world;
    world.load("Locations");
    world.m_Ui.layout();
    QQuickItem* banner = item(world, QStringLiteral("problemBanner"));
    REQUIRE(banner != nullptr);
    CHECK_FALSE(banner->isVisible());

    QQuickItem* north = row_with(world, LOCATIONS, QStringLiteral("North Site"));
    world.m_Ui.tap(find_item(north, QStringLiteral("editorMoveLater")));
    world.m_Ui.tap(find_item(north, QStringLiteral("editorRemove")));
    CHECK(banner->isVisible());
    CHECK(banner->property("message").toString().contains(QStringLiteral("still being saved")));
    world.settle();
    world.m_Ui.layout();
    QMetaObject::invokeMethod(banner, "closeRequested");
    // The banner leaves the column, and the rows move up; they are laid out again before the next touch.
    world.m_Ui.layout();
    CHECK_FALSE(banner->isVisible());

    world.m_Rig.store().fail_next_save(make_error(StorageErrorKind::DISK_FULL, "no room"));
    const QStringList before = names(world);
    world.m_Ui.tap(find_item(row_with(world, LOCATIONS, QStringLiteral("North Site")), QStringLiteral("editorRemove")));
    world.settle();
    CHECK(banner->isVisible());
    CHECK(names(world) == before);
}

TEST_CASE("editor.voiceover: the location rows and buttons are named and the screen passes the checks", "[ui][editor]")
{
    World world;
    QQuickItem* screen = world.load("Locations");
    world.m_Ui.layout();

    QQuickItem* north = row_with(world, LOCATIONS, QStringLiteral("North Site"));
    CHECK(name_of(label_of(north)) == QStringLiteral("North Site"));
    CHECK(name_of(find_item(north, QStringLiteral("editorRemove"))) == QStringLiteral("Remove North Site"));
    CHECK(name_of(find_item(north, QStringLiteral("editorMoveLater"))) == QStringLiteral("Move North Site later"));
    CHECK(name_of(find_item(north, QStringLiteral("editorMoveEarlier"))) == QStringLiteral("Move North Site earlier"));
    CHECK(name_of(find_item(screen, QStringLiteral("navLeading"))) == QStringLiteral("Back to Settings"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(false);

    world.m_Ui.tap(label_of(north));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(name_of(panel(world)) == QStringLiteral("Location"));
    expect_clean(sheet_violations(world));
}
