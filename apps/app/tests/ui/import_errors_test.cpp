// SPDX-License-Identifier: GPL-3.0-or-later
// ImportErrors (docs/plan/stage-4.md, 4.13): import.errors (the messages as they are, in file order, with the
// reassurance that nothing changed), import.voiceover and the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

const QStringList MESSAGES{
    QStringLiteral("Error type 3 has no name."), QStringLiteral("Locations 1 and 2 are both called North Site."),
    QStringLiteral("Note template 1: the line in the log has no {value} where the answer goes.")};

} // namespace

TEST_CASE("import.errors: every message is a row, in the order of the file", "[ui][import]")
{
    World world;
    world.load("ImportErrors", {{"messages", MESSAGES}});
    world.m_Ui.layout();

    CHECK(row_names(world, QStringLiteral("messageRow")) == MESSAGES);
    CHECK(item(world, QStringLiteral("messageGroup"))->property("title").toString() == QStringLiteral("3 problems"));
    CHECK(shows_text(world, QStringLiteral("This file can’t be imported")));
    CHECK(
        shows_text(world, QStringLiteral("Nothing was changed. Fix these in the file, in this order, and try again.")));
}

TEST_CASE("import.errors: each message has the warning sign, and the buttons follow the design", "[ui][import]")
{
    World world;
    world.load("ImportErrors", {{"messages", MESSAGES}});
    world.m_Ui.layout();

    // The design: a red warning triangle at the start of every row, rows 52 pt high at least, and the file icon on the
    // primary button, with Cancel a plain button under it.
    const std::vector<QQuickItem*> rows = items(world, QStringLiteral("messageRow"));
    REQUIRE(rows.size() == 3);
    for (QQuickItem* row : rows)
    {
        CHECK(row->property("leadingIcon").toString() == QStringLiteral("warning"));
        CHECK(row->property("minimumHeight").toDouble() == 52.0);
        CHECK(row->height() >= 52.0);
    }
    CHECK(item(world, QStringLiteral("chooseAnotherButton"))->property("iconKind").toString() ==
          QStringLiteral("file"));
    CHECK(item(world, QStringLiteral("cancelButton"))->property("variant").toInt() == 2);
}

TEST_CASE("import.errors: one problem is said in the singular, and a long message wraps whole", "[ui][import]")
{
    World world;
    const QString long_message = QStringLiteral("/note_templates/1/output_pattern: ") + QString(300, QLatin1Char('x'));
    world.load("ImportErrors", {{"messages", QStringList{long_message}}});
    world.m_Ui.layout();

    CHECK(item(world, QStringLiteral("messageGroup"))->property("title").toString() == QStringLiteral("1 problem"));
    CHECK(row_names(world, QStringLiteral("messageRow")) == QStringList{long_message});
    expect_clean(check_text_scale(world.m_Ui));
}

TEST_CASE("import.errors: Choose another file and the two ways out are signals", "[ui][import]")
{
    World world;
    QQuickItem* page = world.load("ImportErrors", {{"messages", MESSAGES}});
    world.m_Ui.layout();
    QSignalSpy another(page, SIGNAL(chooseAnotherRequested()));
    QSignalSpy cancel(page, SIGNAL(cancelRequested()));

    tap(world, QStringLiteral("chooseAnotherButton"));
    CHECK(another.count() == 1);
    tap(world, QStringLiteral("cancelButton"));
    world.m_Ui.tap(find_item(page, QStringLiteral("navLeading")));
    CHECK(cancel.count() == 2);
    CHECK(another.count() == 1);
}

TEST_CASE("import.voiceover: each message is read as text, the buttons are named and the screen passes the checks",
          "[ui][import]")
{
    World world;
    world.load("ImportErrors", {{"messages", MESSAGES}});
    world.m_Ui.layout();

    CHECK(name_of(item(world, QStringLiteral("chooseAnotherButton"))) == QStringLiteral("Choose another file"));
    CHECK(name_of(item(world, QStringLiteral("cancelButton"))) == QStringLiteral("Cancel"));
    QQuickItem* first = items(world, QStringLiteral("messageRow")).front();
    CHECK(name_of(first) == MESSAGES.front());

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
}
