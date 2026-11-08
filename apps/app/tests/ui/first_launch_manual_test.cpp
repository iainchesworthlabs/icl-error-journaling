// SPDX-License-Identifier: GPL-3.0-or-later
// FirstLaunchManual (docs/plan/stage-4.md, 4.13): setup.first_launch (the facilitator and the UNIT prefix, then
// Continue; held in memory until both are set), setup.voiceover and the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

using Start = World::Start;

} // namespace

TEST_CASE("setup.first_launch: the page asks for the facilitator and the prefix and says where the rest is set",
          "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    world.load("FirstLaunchManual");
    world.m_Ui.layout();

    CHECK(field_text(world, QStringLiteral("facilitatorField")).isEmpty());
    CHECK(field_text(world, QStringLiteral("prefixField")).isEmpty());
    CHECK(shows_text(world, QStringLiteral("About you")));
    CHECK(shows_text(world, QStringLiteral("Error types, locations and note templates are added in Settings.")));

    // The line under the prefix is the model's own (`unitPrefixHint`), and the two fields are the design's 48 pt ones
    // with a hairline border at rest.
    CHECK(item(world, QStringLiteral("prefixField"))->property("hint").toString() == world.model().unit_prefix_hint());
    CHECK(shows_text(world, world.model().unit_prefix_hint()));
    for (const char* name : {"facilitatorField", "prefixField"})
    {
        CHECK(item(world, QString::fromLatin1(name))->property("compact").toBool());
        CHECK(item(world, QString::fromLatin1(name))->property("border").toBool());
    }
}

TEST_CASE("setup.first_launch: the Continue button and its line end at the bottom of the window", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    world.load("FirstLaunchManual");
    world.m_Ui.layout();

    QQuickItem* button = item(world, QStringLiteral("continueButton"));
    CHECK(button->height() >= 54.0);
    QQuickItem* column = button->parentItem();
    REQUIRE(column != nullptr);
    CHECK(column->mapToItem(world.m_Ui.root(), QPointF(0, 0)).y() + column->height() == world.m_Ui.root()->height());
}

TEST_CASE("setup.first_launch: one value alone is held in memory and the other is asked for", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* page = world.load("FirstLaunchManual");
    world.m_Ui.layout();
    QSignalSpy completed(page, SIGNAL(completed()));

    enter_text(world, QStringLiteral("facilitatorField"), QStringLiteral("Sam Example"));
    tap(world, QStringLiteral("continueButton"));

    CHECK(completed.count() == 0);
    CHECK(field_error(world, QStringLiteral("facilitatorField")).isEmpty());
    CHECK_FALSE(field_error(world, QStringLiteral("prefixField")).isEmpty());
    // The model has the name in its set-up and has saved nothing; the app is still not configured.
    CHECK(world.model().facilitator() == QStringLiteral("Sam Example"));
    CHECK_FALSE(world.model().configured());
    CHECK(world.m_Rig.store().saves().empty());

    // Then the prefix: the setup is complete, and saved as one.
    enter_text(world, QStringLiteral("prefixField"), QStringLiteral("UNIT"));
    CHECK(field_error(world, QStringLiteral("prefixField")).isEmpty());
    tap(world, QStringLiteral("continueButton"));
    CHECK(completed.count() == 1);
    CHECK(world.model().configured());
    CHECK(world.m_Rig.store().saves().size() == 1);
    CHECK(world.model().unit_prefix() == QStringLiteral("UNIT"));
}

TEST_CASE("setup.first_launch: an empty name, a prefix with a dash and a prefix of 21 characters are refused inline",
          "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* page = world.load("FirstLaunchManual");
    world.m_Ui.layout();
    QSignalSpy completed(page, SIGNAL(completed()));

    // Nothing typed: both are refused, each under its field.
    tap(world, QStringLiteral("continueButton"));
    CHECK_FALSE(field_error(world, QStringLiteral("facilitatorField")).isEmpty());
    CHECK_FALSE(field_error(world, QStringLiteral("prefixField")).isEmpty());

    enter_text(world, QStringLiteral("facilitatorField"), QStringLiteral("Sam Example"));
    CHECK(field_error(world, QStringLiteral("facilitatorField")).isEmpty());
    enter_text(world, QStringLiteral("prefixField"), QStringLiteral("UNIT-"));
    tap(world, QStringLiteral("continueButton"));
    CHECK_FALSE(field_error(world, QStringLiteral("prefixField")).isEmpty());
    CHECK(completed.count() == 0);

    enter_text(world, QStringLiteral("prefixField"), QString(21, QLatin1Char('U')));
    tap(world, QStringLiteral("continueButton"));
    CHECK_FALSE(field_error(world, QStringLiteral("prefixField")).isEmpty());
    CHECK(completed.count() == 0);
    CHECK_FALSE(world.model().configured());

    enter_text(world, QStringLiteral("prefixField"), QString(20, QLatin1Char('U')));
    tap(world, QStringLiteral("continueButton"));
    CHECK(completed.count() == 1);
    CHECK(world.model().configured());
}

TEST_CASE("setup.first_launch: Return in the prefix field continues and Back asks to go back", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* page = world.load("FirstLaunchManual");
    world.m_Ui.layout();
    QSignalSpy completed(page, SIGNAL(completed()));
    QSignalSpy back(page, SIGNAL(backRequested()));

    world.m_Ui.tap(find_item(page, QStringLiteral("navLeading")));
    CHECK(back.count() == 1);

    enter_text(world, QStringLiteral("facilitatorField"), QStringLiteral("Sam Example"));
    enter_text(world, QStringLiteral("prefixField"), QStringLiteral("UNIT"));
    world.m_Ui.key(Qt::Key_Return);
    world.settle();
    CHECK(completed.count() == 1);
    CHECK(world.model().configured());
}

TEST_CASE("setup.voiceover: the fields and Continue are named and the page passes the checks", "[ui][setup]")
{
    World world(Start::FIRST_LAUNCH);
    QQuickItem* page = world.load("FirstLaunchManual");
    world.m_Ui.layout();

    CHECK(name_of(find_item(item(world, QStringLiteral("facilitatorField")), QStringLiteral("formInput"))) ==
          QStringLiteral("Facilitator"));
    CHECK(name_of(find_item(item(world, QStringLiteral("prefixField")), QStringLiteral("formInput"))) ==
          QStringLiteral("UNIT prefix"));
    CHECK(name_of(item(world, QStringLiteral("continueButton"))) == QStringLiteral("Continue"));
    CHECK(name_of(find_item(page, QStringLiteral("navLeading"))) == QStringLiteral("Back"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));

    // With a refusal under both fields.
    world.m_Ui.set_dark(false);
    tap(world, QStringLiteral("continueButton"));
    expect_clean(check_everything(world.m_Ui));
}
