// SPDX-License-Identifier: GPL-3.0-or-later
// ImportConfirm (docs/plan/stage-4.md, 4.13): import.flow (the summary shows the counts; Replace and Cancel are
// signals), import.voiceover and the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

QVariantMap summary()
{
    return {{"fileName", QStringLiteral("field-test-setup.ejconfig")},
            {"errorTypeCount", 11},
            {"locationCount", 2},
            {"templateCount", 2}};
}

QString value_of(World& world, const QString& row)
{
    return item(world, row)->property("value").toString();
}

} // namespace

TEST_CASE("import.flow: the confirmation shows the file and the counts of what it holds", "[ui][import]")
{
    World world;
    world.load("ImportConfirm", summary());
    world.m_Ui.layout();

    CHECK(item(world, QStringLiteral("fromText"))->property("text").toString() ==
          QStringLiteral("From field-test-setup.ejconfig"));
    CHECK(value_of(world, QStringLiteral("errorTypesRow")) == QStringLiteral("11"));
    CHECK(value_of(world, QStringLiteral("locationsRow")) == QStringLiteral("2"));
    CHECK(value_of(world, QStringLiteral("templatesRow")) == QStringLiteral("2"));
    CHECK(value_of(world, QStringLiteral("preferencesRow")) == QStringLiteral("Included"));
    CHECK(shows_text(world, QStringLiteral("Import this setup?")));

    // The warning says that the setup is replaced and past logs are not touched.
    QQuickItem* banner = item(world, QStringLiteral("replaceBanner"));
    CHECK(banner->property("title").toString() == QStringLiteral("This replaces your current setup"));
    CHECK(banner->property("message").toString().contains(QStringLiteral("Past logs")));
    CHECK(item(world, QStringLiteral("replaceButton"))->property("text").toString() ==
          QStringLiteral("Replace my setup"));
}

TEST_CASE("import.flow: the page is laid out as the design has it", "[ui][import]")
{
    World world;
    QQuickItem* page = world.load("ImportConfirm", summary());
    world.m_Ui.layout();

    // Cancel has a back chevron, the counts end 8 pt before the row padding does, and Cancel is a plain 54 pt button.
    QQuickItem* chevron = find_item(page, QStringLiteral("navBackChevron"));
    REQUIRE(chevron != nullptr);
    CHECK(chevron->isVisible());
    for (const char* row : {"errorTypesRow", "locationsRow", "templatesRow", "preferencesRow"})
    {
        CHECK(item(world, QString::fromLatin1(row))->property("valueInset").toDouble() == 8.0);
    }
    QQuickItem* cancel = item(world, QStringLiteral("cancelButton"));
    CHECK(cancel->property("variant").toInt() == 2);
    CHECK(cancel->height() >= 54.0);
    // The two buttons end at the bottom of the safe area, 10 pt apart.
    QQuickItem* replace = item(world, QStringLiteral("replaceButton"));
    const double gap = cancel->mapToItem(world.m_Ui.root(), QPointF(0, 0)).y() -
                       (replace->mapToItem(world.m_Ui.root(), QPointF(0, 0)).y() + replace->height());
    CHECK(gap == 10.0);
    CHECK(cancel->mapToItem(world.m_Ui.root(), QPointF(0, 0)).y() + cancel->height() == world.m_Ui.root()->height());
}

TEST_CASE("import.flow: with nothing to replace the words say the file becomes the setup", "[ui][import]")
{
    World world;
    QVariantMap properties = summary();
    properties.insert(QStringLiteral("replacing"), false);
    world.load("ImportConfirm", properties);
    world.m_Ui.layout();

    CHECK(item(world, QStringLiteral("replaceBanner"))->property("title").toString() ==
          QStringLiteral("This becomes your setup"));
    CHECK(item(world, QStringLiteral("replaceButton"))->property("text").toString() ==
          QStringLiteral("Use this setup"));
}

TEST_CASE("import.flow: Replace and the two Cancels are signals and change nothing themselves", "[ui][import]")
{
    World world;
    QQuickItem* page = world.load("ImportConfirm", summary());
    world.m_Ui.layout();
    QSignalSpy replace(page, SIGNAL(replaceRequested()));
    QSignalSpy cancel(page, SIGNAL(cancelRequested()));

    tap(world, QStringLiteral("replaceButton"));
    CHECK(replace.count() == 1);
    CHECK(cancel.count() == 0);

    tap(world, QStringLiteral("cancelButton"));
    world.m_Ui.tap(find_item(page, QStringLiteral("navLeading")));
    CHECK(cancel.count() == 2);
    CHECK(replace.count() == 1);
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("import.flow: a failure to apply the file is shown above the buttons", "[ui][import]")
{
    World world;
    QVariantMap properties = summary();
    world.load("ImportConfirm", properties);
    world.m_Ui.layout();
    CHECK_FALSE(item(world, QStringLiteral("problemBanner"))->isVisible());

    world.m_Ui.root()->setProperty("problem", QStringLiteral("There is no room to save the settings."));
    world.m_Ui.layout();
    CHECK(item(world, QStringLiteral("problemBanner"))->isVisible());
    CHECK(item(world, QStringLiteral("problemBanner"))->property("message").toString() ==
          QStringLiteral("There is no room to save the settings."));
}

TEST_CASE("import.voiceover: the rows are read as label and value and the screen passes the checks", "[ui][import]")
{
    World world;
    QVariantMap properties = summary();
    world.load("ImportConfirm", properties);
    world.m_Ui.layout();

    CHECK(name_of(item(world, QStringLiteral("errorTypesRow"))) == QStringLiteral("Error types, 11"));
    CHECK(name_of(item(world, QStringLiteral("replaceButton"))) == QStringLiteral("Replace my setup"));
    CHECK(name_of(item(world, QStringLiteral("cancelButton"))) == QStringLiteral("Cancel"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(false);

    world.m_Ui.root()->setProperty("problem", QStringLiteral("There is no room to save the settings."));
    expect_clean(check_everything(world.m_Ui));
}
