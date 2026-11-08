// SPDX-License-Identifier: GPL-3.0-or-later
// FirstLaunch (docs/plan/stage-4.md, 4.13): setup.first_launch (shown while `configured` is false; import a setup file
// or set up by hand and then continue), the load problem banner, setup.voiceover and the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

using Start = World::Start;

/// One of the two buttons at the foot of the screen: "importCard" and "manualCard".
QQuickItem* card(World& world, const QString& name)
{
    return item(world, name);
}

QString button_text(World& world, const QString& name)
{
    return item(world, name)->property("text").toString();
}

/// Lets the page stack finish a push or a pop.
void settle_page(World& world)
{
    world.settle();
    world.play(400);
    world.m_Ui.layout();
}

} // namespace

TEST_CASE("setup.first_launch: the screen is titled CS Bingo Board and offers two choices", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    REQUIRE_FALSE(world.model().configured());
    world.load("FirstLaunch");
    world.m_Ui.layout();

    CHECK(shows_text(world, QStringLiteral("CS Bingo Board")));
    CHECK(item(world, QStringLiteral("appTitle"))->property("text").toString() == QStringLiteral("CS Bingo Board"));
    REQUIRE(card(world, QStringLiteral("importCard")) != nullptr);
    REQUIRE(card(world, QStringLiteral("manualCard")) != nullptr);
    CHECK(button_text(world, QStringLiteral("importCard")) == QStringLiteral("Import a setup file"));
    CHECK(button_text(world, QStringLiteral("manualCard")) == QStringLiteral("Set up by hand"));

    // The design's two buttons: the import is the primary one, with the file icon, and setting up by hand is the
    // secondary one; a line under them says what a setup file holds. The icon of the app is the list.
    CHECK(card(world, QStringLiteral("importCard"))->property("variant").toInt() == 0);
    CHECK(card(world, QStringLiteral("importCard"))->property("iconKind").toString() == QStringLiteral("file"));
    CHECK(card(world, QStringLiteral("manualCard"))->property("variant").toInt() == 1);
    CHECK(card(world, QStringLiteral("manualCard"))->property("iconKind").toString().isEmpty());
    CHECK(card(world, QStringLiteral("importCard"))->height() >= 54);
    CHECK(card(world, QStringLiteral("manualCard"))->height() >= 54);
    CHECK(item(world, QStringLiteral("setupFileNote"))->property("text").toString() ==
          QStringLiteral("A setup file holds your error types, locations and note templates. You can import one later "
                         "in Settings."));
    QQuickItem* icon = item(world, QStringLiteral("appIcon"));
    CHECK(icon->width() == 84);
    CHECK(icon->height() == 84);
    REQUIRE(find_item(icon, QStringLiteral("appIconGlyph")) != nullptr);
    CHECK(find_item(icon, QStringLiteral("appIconGlyph"))->property("kind").toString() == QStringLiteral("list"));

    // The old working name is nowhere on the screen.
    for (const Node& node : world.m_Ui.tree())
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")))
        {
            CHECK_FALSE(node.m_Item->property("text").toString().contains(QStringLiteral("Error Journaling"),
                                                                          Qt::CaseInsensitive));
        }
    }
}

TEST_CASE("setup.first_launch: Import a setup file asks the shell for the file dialog", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* screen = world.load("FirstLaunch");
    world.m_Ui.layout();
    QSignalSpy requested(screen, SIGNAL(importRequested()));

    world.m_Ui.tap(card(world, QStringLiteral("importCard")));
    CHECK(requested.count() == 1);

    // Nothing was loaded or changed by the tap itself: the shell does the rest.
    CHECK_FALSE(world.model().configured());
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("setup.first_launch: Set up by hand asks for the facilitator and the UNIT prefix, and Back returns",
          "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* screen = world.load("FirstLaunch");
    world.m_Ui.layout();

    world.m_Ui.tap(card(world, QStringLiteral("manualCard")));
    settle_page(world);
    REQUIRE(item(world, QStringLiteral("facilitatorField")) != nullptr);
    CHECK(item(world, QStringLiteral("facilitatorField"))->isVisible());
    CHECK(item(world, QStringLiteral("prefixField"))->isVisible());
    CHECK(item(world, QStringLiteral("continueButton"))->isVisible());
    CHECK(shows_text(world, QStringLiteral("About you")));

    // Back goes to the two choices.
    QQuickItem* back = nullptr;
    for (QQuickItem* leading : items(world, QStringLiteral("navLeading")))
    {
        if (leading->isVisible())
        {
            back = leading;
        }
    }
    REQUIRE(back != nullptr);
    world.m_Ui.tap(back);
    settle_page(world);
    CHECK(card(world, QStringLiteral("manualCard"))->isVisible());
    (void)screen;
}

TEST_CASE("setup.first_launch: setting up by hand with both values saves the setup and tells the shell", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* screen = world.load("FirstLaunch");
    world.m_Ui.layout();
    QSignalSpy completed(screen, SIGNAL(completed()));

    world.m_Ui.tap(card(world, QStringLiteral("manualCard")));
    settle_page(world);
    enter_text(world, QStringLiteral("facilitatorField"), QStringLiteral("Sam Example"));
    enter_text(world, QStringLiteral("prefixField"), QStringLiteral("UNIT"));
    tap(world, QStringLiteral("continueButton"));

    CHECK(completed.count() == 1);
    CHECK(world.model().configured());
    CHECK(world.model().facilitator() == QStringLiteral("Sam Example"));
    CHECK(world.model().unit_prefix() == QStringLiteral("UNIT"));
    CHECK(world.m_Rig.store().saves().size() == 1);
}

TEST_CASE("setup.first_launch: a saved file that could not be used is a banner above the choices", "[ui][setup]")
{
    World world(Start::DAMAGED_FILE);
    REQUIRE_FALSE(world.model().load_problem().isEmpty());
    world.load("FirstLaunch");
    world.m_Ui.layout();

    QQuickItem* banner = item(world, QStringLiteral("loadProblemBanner"));
    REQUIRE(banner != nullptr);
    CHECK(banner->isVisible());
    CHECK(banner->property("message").toString() == world.model().load_problem());
    CHECK(card(world, QStringLiteral("importCard"))->isVisible());

    // It can be dismissed, and the choices stay.
    QMetaObject::invokeMethod(banner, "closeRequested");
    world.m_Ui.layout();
    CHECK_FALSE(banner->isVisible());
    CHECK(card(world, QStringLiteral("manualCard"))->isVisible());

    // A first launch with no file has no banner.
    World clean(Start::FIRST_LAUNCH);
    clean.load("FirstLaunch");
    clean.m_Ui.layout();
    CHECK_FALSE(item(clean, QStringLiteral("loadProblemBanner"))->isVisible());
}

TEST_CASE("setup.voiceover: the choices are named buttons, and the screen passes the checks", "[ui][setup]")
{
    World world(Start::DAMAGED_FILE);
    world.load("FirstLaunch");
    world.m_Ui.layout();

    QAccessibleInterface* import_card =
        QAccessible::queryAccessibleInterface(card(world, QStringLiteral("importCard")));
    REQUIRE(import_card != nullptr);
    CHECK(import_card->role() == QAccessible::Button);
    CHECK(import_card->text(QAccessible::Name) == QStringLiteral("Import a setup file"));
    CHECK(name_of(card(world, QStringLiteral("manualCard"))) == QStringLiteral("Set up by hand"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
}
