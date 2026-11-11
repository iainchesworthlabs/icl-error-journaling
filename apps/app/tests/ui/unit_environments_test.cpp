// SPDX-License-Identifier: GPL-3.0-or-later
// The unit environments editor and its rename sheet (docs/plan/stage-7.md, 7.11; PRF-11): editor.environments and
// editor.voiceover for it.
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

constexpr auto ENVIRONMENTS = "environmentRow";

QStringList names(World& world)
{
    return row_names(world, ENVIRONMENTS);
}

/// Types `text` into the field of the new environment and taps "Add environment".
void add_environment(World& world, const QString& text)
{
    enter_text(world, QStringLiteral("newEnvironmentField"), text);
    // A message under the field goes when it is edited, and the button under it moves up: it is laid out before the tap.
    world.m_Ui.layout();
    tap(world, QStringLiteral("addButton"));
}

QStringList names_in_model(World& world)
{
    return world.model().unit_environments();
}

} // namespace

TEST_CASE("editor.environments: the environments are listed in order, with a field to add one", "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    CHECK(names(world) == QStringList{QStringLiteral("prod"), QStringLiteral("dev")});
    CHECK(item(world, QStringLiteral("environmentGroup"))->property("title").toString() ==
          QStringLiteral("Environments · 2"));
    CHECK(field_text(world, QStringLiteral("newEnvironmentField")).isEmpty());
    CHECK(item(world, QStringLiteral("environmentGroup"))->property("footer").toString() ==
          QStringLiteral("Today’s details offers these in this order, after “None”. Removing one doesn’t change past "
                         "logs."));
}

TEST_CASE("editor.environments: adding puts it last, lowers capitals as they are typed, and clears the field",
          "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    enter_text(world, QStringLiteral("newEnvironmentField"), QStringLiteral("Staging"));
    CHECK(field_text(world, QStringLiteral("newEnvironmentField")) == QStringLiteral("staging"));
    world.m_Ui.layout();
    tap(world, QStringLiteral("addButton"));
    CHECK(names(world) == QStringList{QStringLiteral("prod"), QStringLiteral("dev"), QStringLiteral("staging")});
    CHECK(names_in_model(world) == names(world));
    CHECK(field_text(world, QStringLiteral("newEnvironmentField")).isEmpty());
    CHECK(field_error(world, QStringLiteral("newEnvironmentField")).isEmpty());
}

TEST_CASE("editor.environments: a name that is empty, not allowed or repeated is refused under the field",
          "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    // Nothing typed.
    tap(world, QStringLiteral("addButton"));
    CHECK_FALSE(field_error(world, QStringLiteral("newEnvironmentField")).isEmpty());
    CHECK(names(world).size() == 2);

    // A name with a space in it: the text stays so that it can be corrected, and the next edit clears the message.
    add_environment(world, QStringLiteral("pre prod"));
    CHECK(field_error(world, QStringLiteral("newEnvironmentField")).contains(QStringLiteral("lower-case letters")));
    CHECK(field_text(world, QStringLiteral("newEnvironmentField")) == QStringLiteral("pre prod"));
    enter_text(world, QStringLiteral("newEnvironmentField"), QStringLiteral("pre-prod"));
    CHECK(field_error(world, QStringLiteral("newEnvironmentField")).isEmpty());

    // One that is there already, and one that starts with a dash.
    add_environment(world, QStringLiteral("DEV"));
    CHECK(field_error(world, QStringLiteral("newEnvironmentField")).contains(QStringLiteral("dev")));
    add_environment(world, QStringLiteral("-dev"));
    CHECK_FALSE(field_error(world, QStringLiteral("newEnvironmentField")).isEmpty());
    CHECK(names(world).size() == 2);

    // 21 characters are refused and 20 are saved.
    add_environment(world, QString(21, QLatin1Char('x')));
    CHECK_FALSE(field_error(world, QStringLiteral("newEnvironmentField")).isEmpty());
    CHECK(names(world).size() == 2);
    add_environment(world, QString(20, QLatin1Char('x')));
    CHECK(names(world).size() == 3);
}

TEST_CASE("editor.environments: a row opens a sheet to rename the environment", "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    world.m_Ui.tap(label_of(row_with(world, ENVIRONMENTS, QStringLiteral("dev"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(field_text(world, QStringLiteral("renameField")) == QStringLiteral("dev"));

    // A repeated name is refused in the sheet, which stays.
    enter_text(world, QStringLiteral("renameField"), QStringLiteral("PROD"));
    CHECK(field_text(world, QStringLiteral("renameField")) == QStringLiteral("prod"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(field_error(world, QStringLiteral("renameField")).isEmpty());
    CHECK_FALSE(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());

    // A good one is saved in the same place.
    enter_text(world, QStringLiteral("renameField"), QStringLiteral("test"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world) == QStringList{QStringLiteral("prod"), QStringLiteral("test")});

    // Cancel leaves the name.
    world.m_Ui.tap(label_of(row_with(world, ENVIRONMENTS, QStringLiteral("test"))));
    world.play(400);
    enter_text(world, QStringLiteral("renameField"), QStringLiteral("other"));
    tap_in_sheet(world, QStringLiteral("navLeading"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world) == QStringList{QStringLiteral("prod"), QStringLiteral("test")});
}

TEST_CASE("editor.environments: Remove takes it out of the list, and the last one cannot be removed", "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    world.m_Ui.tap(find_item(row_with(world, ENVIRONMENTS, QStringLiteral("prod")), QStringLiteral("editorRemove")));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("dev")});
    CHECK(names_in_model(world) == QStringList{QStringLiteral("dev")});

    // The last one stays, with the reason in the banner.
    QQuickItem* banner = item(world, QStringLiteral("problemBanner"));
    REQUIRE(banner != nullptr);
    CHECK_FALSE(banner->isVisible());
    world.m_Ui.tap(find_item(row_with(world, ENVIRONMENTS, QStringLiteral("dev")), QStringLiteral("editorRemove")));
    world.settle();
    world.m_Ui.layout();
    CHECK(banner->isVisible());
    CHECK(banner->property("message").toString().contains(QStringLiteral("at least one")));
    CHECK(names(world) == QStringList{QStringLiteral("dev")});
}

TEST_CASE("editor.environments: VoiceOver reorders with the handle's actions, 0-based, and the ends cannot move "
          "past the list",
          "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    QQuickItem* prod = row_with(world, ENVIRONMENTS, QStringLiteral("prod"));
    // The first one cannot move earlier: the action does nothing.
    REQUIRE(accessible_adjust(find_item(prod, QStringLiteral("editorHandle")), false));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("prod"), QStringLiteral("dev")});

    REQUIRE(accessible_adjust(find_item(prod, QStringLiteral("editorHandle")), true));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("dev"), QStringLiteral("prod")});
    CHECK(names_in_model(world) == names(world));
}

TEST_CASE("editor.environments: dragging a handle by whole rows moves the environment", "[ui][editor]")
{
    World world;
    world.load("UnitEnvironments");
    world.m_Ui.layout();

    QQuickItem* prod = row_with(world, ENVIRONMENTS, QStringLiteral("prod"));
    QQuickItem* handle = find_item(prod, QStringLiteral("editorHandle"));
    REQUIRE(handle != nullptr);
    const double row_height = world.m_Ui.geometry(prod).height();
    const QPointF from(handle->width() / 2, handle->height() / 2);
    world.m_Ui.drag(handle, from, from + QPointF(0, row_height * 3), 12);
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world) == QStringList{QStringLiteral("dev"), QStringLiteral("prod")});
}

TEST_CASE("editor.voiceover: the environment rows and buttons are named and the screen passes the checks",
          "[ui][editor]")
{
    World world;
    QQuickItem* screen = world.load("UnitEnvironments");
    world.m_Ui.layout();

    QQuickItem* prod = row_with(world, ENVIRONMENTS, QStringLiteral("prod"));
    CHECK(name_of(label_of(prod)) == QStringLiteral("prod"));
    CHECK(name_of(find_item(prod, QStringLiteral("editorRemove"))) == QStringLiteral("Remove prod"));
    CHECK(name_of(find_item(prod, QStringLiteral("editorHandle"))) == QStringLiteral("Reorder prod"));
    CHECK(name_of(find_item(screen, QStringLiteral("navLeading"))) == QStringLiteral("Back to Settings"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(false);

    world.m_Ui.tap(label_of(prod));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(name_of(panel(world)) == QStringLiteral("Environment"));
    expect_clean(sheet_violations(world));
}
