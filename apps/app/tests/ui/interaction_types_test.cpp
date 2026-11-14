// SPDX-License-Identifier: GPL-3.0-or-later
// InteractionTypes, the interaction types editor (Stage 14, package I-7): T-I7-1 `settings.interaction_types`.
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>

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

TEST_CASE("settings.interaction_types: the types are listed with their line, and the first row adds one",
          "[ui][editor][interaction_types]")
{
    World world;
    world.load("InteractionTypes");
    world.m_Ui.layout();

    const QStringList listed = names(world);
    REQUIRE(listed.size() >= 2);
    CHECK(names(world) == listed);
    CHECK(listed.front() == QStringLiteral("Spoke with"));
    QQuickItem* first = row_with(world, TYPES, QStringLiteral("Spoke with"));
    REQUIRE(first != nullptr);
    CHECK(first->property("detail").toString() == QStringLiteral("Spoke with [person]"));
    CHECK(item(world, QStringLiteral("addRow"))->isVisible());
}

TEST_CASE("settings.interaction_types: the miniature board shows the tiles, and an empty tile gets a type from the picker",
          "[ui][editor][interaction_types]")
{
    World world;
    world.load("InteractionTypes");
    world.m_Ui.layout();

    QQuickItem* first = find_item(world.m_Ui.root(), QStringLiteral("miniTile0"));
    REQUIRE(first != nullptr);
    CHECK(child(first, QStringLiteral("miniLabel"))->property("text").toString() == QStringLiteral("Spoke with"));
    // The sample has six types, on the tiles 1 to 6, so the seventh is empty.
    QQuickItem* empty = find_item(world.m_Ui.root(), QStringLiteral("miniTile6"));
    REQUIRE(empty != nullptr);
    CHECK(find_item(world.m_Ui.root(), QStringLiteral("miniTile5"))->property("isEmpty").toBool() == false);
    CHECK(empty->property("isEmpty").toBool());

    world.m_Ui.tap(empty);
    world.play(400);
    world.m_Ui.layout();
    // The editor holds two sheets (the type's and this one), so the picker is found by its own bar.
    QQuickItem* bar = find_item(world.m_Ui.root(), QStringLiteral("tilePickerBar"));
    REQUIRE(bar != nullptr);
    REQUIRE(bar->isVisible());
    CHECK(bar->property("title").toString() == QStringLiteral("Add interaction type"));

    QQuickItem* helped = row_with(world, QStringLiteral("tilePickerType"), QStringLiteral("Helped"));
    REQUIRE(helped != nullptr);
    world.m_Ui.tap(helped);
    world.settle();
    world.play(400);
    world.m_Ui.layout();
    CHECK_FALSE(find_item(world.m_Ui.root(), QStringLiteral("tilePickerBar"))->isVisible());
    QQuickItem* now = find_item(world.m_Ui.root(), QStringLiteral("miniTile6"));
    CHECK(child(now, QStringLiteral("miniLabel"))->property("text").toString() == QStringLiteral("Helped"));
    // It left tile 6 for tile 7.
    CHECK(find_item(world.m_Ui.root(), QStringLiteral("miniTile5"))->property("isEmpty").toBool());
}
