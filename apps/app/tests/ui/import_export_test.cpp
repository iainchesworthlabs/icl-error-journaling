// SPDX-License-Identifier: GPL-3.0-or-later
// ImportExport (docs/plan/stage-4.md, 4.13 and 4.12): the import and export rows emit signals the shell routes, the
// result of an export is a banner, import.voiceover and the cross-cutting checks.
#include "editors_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

TEST_CASE("import.rows: the import and export rows only emit signals", "[ui][import]")
{
    World world;
    QQuickItem* page = world.load("ImportExport");
    world.m_Ui.layout();
    QSignalSpy import_requested(page, SIGNAL(importRequested()));
    QSignalSpy export_requested(page, SIGNAL(exportRequested()));

    tap(world, QStringLiteral("importRow"));
    CHECK(import_requested.count() == 1);
    CHECK(export_requested.count() == 0);
    tap(world, QStringLiteral("exportRow"));
    CHECK(export_requested.count() == 1);
    CHECK(import_requested.count() == 1);

    // The page asked the model for nothing: choosing the file is the shell's.
    CHECK(world.m_Rig.m_Gateway.pending() == 0);
    CHECK(world.m_Rig.store().files().empty());
    CHECK(shows_text(world, QStringLiteral("Import or export")));
}

TEST_CASE("import.rows: the result of an export is shown in a banner that can be dismissed", "[ui][import]")
{
    World world;
    world.load("ImportExport");
    world.m_Ui.layout();
    QQuickItem* banner = item(world, QStringLiteral("exportBanner"));
    REQUIRE(banner != nullptr);
    CHECK_FALSE(banner->isVisible());

    // The shell calls exportTo with the path the person chose.
    world.model().exportTo(QStringLiteral("/out/cs-bingo-board-setup.ejconfig"));
    world.settle();
    world.m_Ui.layout();
    CHECK(banner->isVisible());
    CHECK(banner->property("title").toString() == QStringLiteral("Setup exported"));
    CHECK(banner->property("message").toString() == QStringLiteral("The settings were exported."));
    CHECK(banner->property("tone").toInt() == 0);
    CHECK(world.m_Rig.store().files().at("/out/cs-bingo-board-setup.ejconfig") == sample_text());

    QMetaObject::invokeMethod(banner, "closeRequested");
    world.m_Ui.layout();
    CHECK_FALSE(banner->isVisible());
}

TEST_CASE("import.rows: an export that cannot be made is an error banner", "[ui][import]")
{
    World world(World::Start::FIRST_LAUNCH);
    world.load("ImportExport");
    world.m_Ui.layout();

    world.model().exportTo(QStringLiteral("/out/cs-bingo-board-setup.ejconfig"));
    world.settle();
    world.m_Ui.layout();
    QQuickItem* banner = item(world, QStringLiteral("exportBanner"));
    CHECK(banner->isVisible());
    CHECK(banner->property("title").toString() == QStringLiteral("Setup not exported"));
    CHECK(banner->property("tone").toInt() == 2);
    CHECK(banner->property("message").toString().contains(QStringLiteral("nothing to export")));
}

TEST_CASE("import.rows: the Back button asks to close", "[ui][import]")
{
    World world;
    QQuickItem* page = world.load("ImportExport");
    world.m_Ui.layout();
    QSignalSpy closed(page, SIGNAL(closeRequested()));
    world.m_Ui.tap(find_item(page, QStringLiteral("navLeading")));
    CHECK(closed.count() == 1);
}

TEST_CASE("import.voiceover: the rows are buttons with names and the page passes the checks", "[ui][import]")
{
    World world;
    QQuickItem* page = world.load("ImportExport");
    world.m_Ui.layout();

    CHECK(name_of(item(world, QStringLiteral("importRow"))) == QStringLiteral("Import a setup file…"));
    CHECK(name_of(item(world, QStringLiteral("exportRow"))) == QStringLiteral("Export this setup…"));
    CHECK(name_of(find_item(page, QStringLiteral("navLeading"))) == QStringLiteral("Back to Settings"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(false);

    world.model().exportTo(QStringLiteral("/out/cs-bingo-board-setup.ejconfig"));
    world.settle();
    world.m_Ui.layout();
    expect_clean(check_everything(world.m_Ui));
}
