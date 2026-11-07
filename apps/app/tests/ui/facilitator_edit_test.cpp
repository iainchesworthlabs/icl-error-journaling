// SPDX-License-Identifier: GPL-3.0-or-later
// FacilitatorEdit (docs/plan/stage-4.md, 4.13 and 4.12): the page that changes the facilitator, editor.voiceover and
// the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

bool save_enabled(QQuickItem* page)
{
    return find_item(page, QStringLiteral("navTrailing"))->isEnabled();
}

void tap_save(World& world, QQuickItem* page)
{
    world.m_Ui.tap(find_item(page, QStringLiteral("navTrailing")));
    world.settle();
    world.m_Ui.layout();
}

} // namespace

TEST_CASE("editor.facilitator: the page shows the name in force and says when a new one applies", "[ui][editor]")
{
    World world;
    world.load("FacilitatorEdit");
    world.m_Ui.layout();

    CHECK(field_text(world, QStringLiteral("facilitatorField")) == QStringLiteral("Sam Example"));
    CHECK(shows_text(world, QStringLiteral("The name at the head of every log you share. It applies from the next log; "
                                           "the open log keeps the name it started with.")));
}

TEST_CASE("editor.facilitator: Save calls the model once, saves the name and closes the page", "[ui][editor]")
{
    World world;
    QQuickItem* page = world.load("FacilitatorEdit");
    world.m_Ui.layout();
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    enter_text(world, QStringLiteral("facilitatorField"), QStringLiteral("Pat Example"));
    tap_save(world, page);

    CHECK(closed.count() == 1);
    CHECK(world.m_Rig.store().saves().size() == 1);
    CHECK(world.model().facilitator() == QStringLiteral("Pat Example"));
}

TEST_CASE("editor.facilitator: an empty or over-long name is refused inline, the old name stays and Save is dimmed",
          "[ui][editor]")
{
    World world;
    QQuickItem* page = world.load("FacilitatorEdit");
    world.m_Ui.layout();
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    enter_text(world, QStringLiteral("facilitatorField"), QString());
    tap_save(world, page);
    CHECK_FALSE(field_error(world, QStringLiteral("facilitatorField")).isEmpty());
    CHECK_FALSE(save_enabled(page));
    CHECK(closed.count() == 0);
    CHECK(world.model().facilitator() == QStringLiteral("Sam Example"));

    enter_text(world, QStringLiteral("facilitatorField"), QString(201, QLatin1Char('a')));
    CHECK(field_error(world, QStringLiteral("facilitatorField")).isEmpty());
    CHECK(save_enabled(page));
    tap_save(world, page);
    CHECK_FALSE(field_error(world, QStringLiteral("facilitatorField")).isEmpty());
    CHECK(world.model().facilitator() == QStringLiteral("Sam Example"));

    enter_text(world, QStringLiteral("facilitatorField"), QString(200, QLatin1Char('a')));
    tap_save(world, page);
    CHECK(closed.count() == 1);
}

TEST_CASE("editor.facilitator: Back closes without saving", "[ui][editor]")
{
    World world;
    QQuickItem* page = world.load("FacilitatorEdit");
    world.m_Ui.layout();
    QSignalSpy closed(page, SIGNAL(closeRequested()));

    enter_text(world, QStringLiteral("facilitatorField"), QStringLiteral("Pat Example"));
    world.m_Ui.tap(find_item(page, QStringLiteral("navLeading")));
    CHECK(closed.count() == 1);
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("editor.voiceover: the facilitator field is named and the page passes the checks", "[ui][editor]")
{
    World world;
    QQuickItem* page = world.load("FacilitatorEdit");
    world.m_Ui.layout();

    CHECK(name_of(find_item(item(world, QStringLiteral("facilitatorField")), QStringLiteral("formInput"))) ==
          QStringLiteral("Facilitator"));
    CHECK(name_of(find_item(page, QStringLiteral("navLeading"))) == QStringLiteral("Back to Settings"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
}
