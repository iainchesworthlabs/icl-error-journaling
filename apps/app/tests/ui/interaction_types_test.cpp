// SPDX-License-Identifier: GPL-3.0-or-later
// InteractionTypes, the interaction types editor (Stage 14, package I-7): T-I7-1 `settings.interaction_types`.
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
