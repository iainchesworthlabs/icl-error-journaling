// SPDX-License-Identifier: GPL-3.0-or-later
// InteractionTypeEdit, the sheet of the interaction types editor (Stage 14, package I-7).
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

constexpr auto TYPES = "typeRow";

QStringList names(World& world)
{
    return row_names(world, TYPES);
}

} // namespace
TEST_CASE("settings.interaction_type_edit: adding a type with [person] saves it and the sheet closes",
          "[ui][editor][interaction_types]")
{
    World world;
    world.load("InteractionTypes");
    world.m_Ui.layout();
    const qsizetype before = names(world).size();

    world.m_Ui.tap(label_of(item(world, QStringLiteral("addRow"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(in_sheet(world, QStringLiteral("retireRow"))->isVisible() == false);

    // An empty name is refused under the field.
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(field_error(world, QStringLiteral("nameField")).isEmpty());

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Greeted"));
    tap_in_sheet(world, QStringLiteral("insertPersonButton"));
    CHECK(in_sheet(world, QStringLiteral("previewText"))->property("text").toString().contains(QStringLiteral("Alex Sample")));
    CHECK(in_sheet(world, QStringLiteral("asksText"))->property("text").toString().contains(QStringLiteral("people")));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world).size() == before + 1);
    CHECK(names(world).back() == QStringLiteral("Greeted"));
}

TEST_CASE("settings.interaction_type_edit: a line with no [person] says that it asks nothing, and a bad line blocks Save",
          "[ui][editor][interaction_types]")
{
    World world;
    world.load("InteractionTypes");
    world.m_Ui.layout();

    world.m_Ui.tap(label_of(item(world, QStringLiteral("addRow"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Waved"));
    CHECK(in_sheet(world, QStringLiteral("asksText"))->property("text").toString().contains(QStringLiteral("asks nothing")));

    enter_text(world, QStringLiteral("lineField"), QStringLiteral("Waved {"));
    CHECK_FALSE(field_error(world, QStringLiteral("lineField")).isEmpty());
    CHECK_FALSE(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());

    enter_text(world, QStringLiteral("lineField"), QStringLiteral(""));
    CHECK(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());
}

TEST_CASE("settings.interaction_type_edit: a row opens the sheet, and retiring removes the type from the list",
          "[ui][editor][interaction_types]")
{
    World world;
    world.load("InteractionTypes");
    world.m_Ui.layout();
    const qsizetype before = names(world).size();

    QQuickItem* row = row_with(world, TYPES, QStringLiteral("Spoke with"));
    REQUIRE(row != nullptr);
    world.m_Ui.tap(label_of(row));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(in_sheet(world, QStringLiteral("retireRow"))->isVisible());

    tap_in_sheet(world, QStringLiteral("retireRow"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world).size() == before - 1);
    CHECK_FALSE(names(world).contains(QStringLiteral("Spoke with")));
}
