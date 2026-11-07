// SPDX-License-Identifier: GPL-3.0-or-later
// Templates (docs/plan/stage-4.md, 4.13): editor.templates (the list, adding and opening a template), editor.voiceover
// and the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

constexpr auto ROWS = "templateRow";

QStringList names(World& world)
{
    return row_names(world, ROWS);
}

/// Lets the page stack finish a push or a pop.
void settle_page(World& world)
{
    world.settle();
    world.play(400);
    world.m_Ui.layout();
}

} // namespace

TEST_CASE("editor.templates: the templates are listed with their question and pattern, and a row opens one",
          "[ui][editor]")
{
    World world;
    world.load("Templates");
    world.m_Ui.layout();

    CHECK(names(world) == QStringList{QStringLiteral("Played song"), QStringLiteral("Visitor interaction")});
    QQuickItem* first = row_with(world, ROWS, QStringLiteral("Played song"));
    REQUIRE(first != nullptr);
    CHECK(first->property("detail").toString() == QStringLiteral("Prompt: Which song? · Output: Played {value}"));
    CHECK(item(world, QStringLiteral("templateGroup"))->property("title").toString() ==
          QStringLiteral("Chips on the Log screen · 2"));

    world.m_Ui.tap(label_of(first));
    settle_page(world);
    // The pushed page has finished its transition: it and everything above it in the tree are shown at full opacity.
    for (QQuickItem* up = item(world, QStringLiteral("nameField"));
         up != nullptr && up != world.m_Ui.root()->parentItem(); up = up->parentItem())
    {
        INFO(up->metaObject()->className());
        CHECK(up->opacity() == 1.0);
        CHECK(up->isVisible());
    }
    CHECK(field_text(world, QStringLiteral("nameField")) == QStringLiteral("Played song"));
    CHECK(field_text(world, QStringLiteral("patternField")) == QStringLiteral("Played {value}"));

    // Back returns to the list, and nothing was saved.
    world.m_Ui.tap(find_item(world.m_Ui.root(), QStringLiteral("navLeading")));
    settle_page(world);
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("editor.templates: Add a template opens an empty page, and a good Save returns to a longer list",
          "[ui][editor]")
{
    World world;
    world.load("Templates");
    world.m_Ui.layout();

    world.m_Ui.tap(label_of(item(world, QStringLiteral("addRow"))));
    settle_page(world);
    REQUIRE(item(world, QStringLiteral("nameField")) != nullptr);
    CHECK(field_text(world, QStringLiteral("nameField")).isEmpty());

    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Requested song"));
    enter_text(world, QStringLiteral("promptField"), QStringLiteral("Which song?"));
    enter_text(world, QStringLiteral("patternField"), QStringLiteral("Song {value} requested"));
    QQuickItem* save = nullptr;
    for (QQuickItem* trailing : items(world, QStringLiteral("navTrailing")))
    {
        if (trailing->isVisible() && name_of(trailing) == QStringLiteral("Save"))
        {
            save = trailing;
        }
    }
    REQUIRE(save != nullptr);
    world.m_Ui.tap(save);
    settle_page(world);

    CHECK(names(world) == QStringList{QStringLiteral("Played song"), QStringLiteral("Visitor interaction"),
                                      QStringLiteral("Requested song")});
}

TEST_CASE("editor.templates: the Settings button asks to close", "[ui][editor]")
{
    World world;
    QQuickItem* screen = world.load("Templates");
    world.m_Ui.layout();
    QSignalSpy closed(screen, SIGNAL(closeRequested()));
    world.m_Ui.tap(find_item(screen, QStringLiteral("navLeading")));
    CHECK(closed.count() == 1);
}

TEST_CASE("editor.voiceover: the template rows are buttons named with their question and pattern", "[ui][editor]")
{
    World world;
    QQuickItem* screen = world.load("Templates");
    world.m_Ui.layout();

    QQuickItem* first = row_with(world, ROWS, QStringLiteral("Played song"));
    CHECK(name_of(label_of(first)) == QStringLiteral("Played song, Prompt: Which song? · Output: Played {value}"));
    CHECK(name_of(label_of(item(world, QStringLiteral("addRow")))) == QStringLiteral("Add a template"));
    CHECK(name_of(find_item(screen, QStringLiteral("navLeading"))) == QStringLiteral("Back to Settings"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
}
