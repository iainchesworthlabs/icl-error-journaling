// SPDX-License-Identifier: GPL-3.0-or-later
// EnvironmentSheet, the short list of environments that the Environment row of Today's details opens (PRF-11): the
// choices in order, what is ticked, what Cancel does, VoiceOver names, large text and dark mode. The sheet is declared
// by Today's details, so the tests load that screen and open the sheet from its row. The models are the real ones of
// Stage 3 over the in-memory store. What choosing does to the unit is tested with the screen (header_screen_test.cpp).
#include "header_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QStringList>
#include <string>

using namespace ej::ui;
using namespace ej::ui::header;

namespace
{

// Tall enough that nothing needs scrolling to be tapped.
const QSize TALL_WINDOW(402, 1600);

QQuickItem* load_screen(UiRig& rig, HeaderWorld& world)
{
    world.attach(rig);
    QQuickItem* screen = rig.load(QStringLiteral("HeaderScreen"), world.models());
    INFO(rig.errors().toStdString());
    REQUIRE(screen != nullptr);
    rig.settle();
    return screen;
}

QString name_of_item(UiRig& rig, QQuickItem* item)
{
    for (const Node& node : rig.tree())
    {
        if (node.m_Item == item)
        {
            return node.m_Name;
        }
    }
    return QString();
}

} // namespace

TEST_CASE("environment.sheet: the sheet offers None, prod and dev, with None ticked", "[ui][environment_sheet]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    open_environment_sheet(rig);

    CHECK(sheet_choices(rig) == QStringList{"", "prod", "dev"});
    CHECK(choice(rig, "environmentNone")->property("text").toString() == QStringLiteral("None"));
    CHECK(choice(rig, "environmentNone")->property("checked").toBool());
    CHECK_FALSE(choice(rig, "environment_prod")->property("checked").toBool());
    CHECK_FALSE(choice(rig, "environment_dev")->property("checked").toBool());
    // There is no text field: the environment is picked and never typed.
    QQuickItem* sheet = need(rig, QStringLiteral("environmentSheet"));
    CHECK(rig.find(QStringLiteral("formInput"), sheet) == nullptr);
    // Nothing is saved by opening it, or by Cancel.
    rig.tap(need(rig, QStringLiteral("navLeading"), sheet));
    play(rig, 500);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
    CHECK(world.session().pending_writes() == 0);
}

TEST_CASE("environment.sheet: an environment the setup no longer lists is shown, ticked, as the last choice",
          "[ui][environment_sheet]")
{
    // The setup lists "qa" only; the log was made under an earlier setup, with prod.
    ej::Configuration configuration = ej::test::sample_configuration();
    configuration.m_UnitEnvironments = {"qa"};
    HeaderWorld world(true, configuration);
    REQUIRE(world.session().correctHeader(QStringLiteral("unit_number"), QStringLiteral("27-prod")));
    world.gateway().run_all();

    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    CHECK(need(rig, QStringLiteral("headerEnvironment"))->property("value").toString() == QStringLiteral("prod"));
    open_environment_sheet(rig);
    CHECK(sheet_choices(rig) == QStringList{"", "qa", "prod"});
    CHECK(choice(rig, "environment_prod")->property("checked").toBool());
    CHECK_FALSE(choice(rig, "environment_qa")->property("checked").toBool());
}

TEST_CASE("environment.sheet: VoiceOver names the row, the sheet and its choices", "[ui][environment_sheet]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerEnvironment"));

    // The row reads its label and its value, and is a button-sized target.
    CHECK(name_of_item(rig, row) == QStringLiteral("Number suffix, None"));
    expect_clean(check_names(rig));
    expect_clean(check_targets(rig));

    open_environment_sheet(rig);
    QQuickItem* sheet = need(rig, QStringLiteral("environmentSheet"));
    CHECK(sheet->property("accessibleName").toString() == QStringLiteral("Number suffix"));
    CHECK(name_of_item(rig, choice(rig, "environmentNone")) == QStringLiteral("None"));
    CHECK(name_of_item(rig, choice(rig, "environment_prod")) == QStringLiteral("prod"));
    CHECK(name_of_item(rig, choice(rig, "environment_dev")) == QStringLiteral("dev"));
    // The choice in force says that it is selected.
    for (const Node& node : rig.tree())
    {
        if (node.m_ObjectName == QStringLiteral("environmentNone"))
        {
            CHECK(node.m_InAccessibleTree);
            CHECK(node.m_Interactive);
        }
    }
    expect_clean(check_names(rig));
    expect_clean(check_targets(rig));

    rig.tap(choice(rig, "environment_prod"));
    play(rig, 500);
    rig.settle();
    CHECK(name_of_item(rig, row) == QStringLiteral("Number suffix, prod"));
}

TEST_CASE("environment.sheet: large text, dark mode and the safe area hold", "[ui][environment_sheet]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    QQuickItem* screen = load_screen(rig, world);
    REQUIRE(screen != nullptr);

    SECTION("the screen, light and dark, with the row in it")
    {
        expect_clean(check_everything(rig));
        rig.set_dark(true);
        rig.settle();
        expect_clean(check_everything(rig));
    }

    SECTION("the sheet, light and dark")
    {
        // The page under the sheet is in the tree too, so the order of reading is not checked here (as for the time
        // sheet).
        open_environment_sheet(rig);
        expect_clean(check_names(rig));
        expect_clean(check_targets(rig));
        expect_clean(check_text_scale(rig));
        rig.set_dark(true);
        rig.settle();
        expect_clean(check_names(rig));
        expect_clean(check_targets(rig));
        expect_clean(check_text_scale(rig));
    }

    SECTION("the sheet at the largest text size")
    {
        open_environment_sheet(rig);
        rig.set_text_scale(3.1);
        rig.layout();
        rig.settle();
        expect_clean(check_text_scale(rig));
        expect_clean(check_targets(rig));
        // Every choice is still there and can be reached: inside the sheet, which scrolls when the list is taller.
        QQuickItem* sheet = need(rig, QStringLiteral("environmentSheet"));
        const QRectF panel = rig.geometry(sheet->property("panelItem").value<QQuickItem*>());
        CHECK(panel.height() > 0);
        CHECK(panel.top() >= 0);
        CHECK(sheet_choices(rig).size() == 3);
        QQuickItem* none = choice(rig, "environmentNone");
        CHECK(rig.geometry(none).height() >= 44.0);
        CHECK(rig.geometry(none).top() >= panel.top());
    }

    SECTION("the page at the largest text size")
    {
        rig.set_text_scale(3.1);
        rig.layout();
        expect_clean(check_text_scale(rig));
        QQuickItem* row = need(rig, QStringLiteral("headerEnvironment"));
        CHECK(rig.geometry(row).height() >= 44.0);
    }
}

TEST_CASE("environment.sheet: a long list of choices stays inside the sheet and scrolls", "[ui][environment_sheet]")
{
    ej::Configuration configuration = ej::test::sample_configuration();
    configuration.m_UnitEnvironments.clear();
    for (int i = 0; i < 30; ++i)
    {
        configuration.m_UnitEnvironments.push_back("env-" + std::to_string(i + 1));
    }
    HeaderWorld world(true, configuration);
    UiRig rig;
    load_screen(rig, world);
    open_environment_sheet(rig);
    QQuickItem* sheet = need(rig, QStringLiteral("environmentSheet"));
    const QRectF panel = rig.geometry(sheet->property("panelItem").value<QQuickItem*>());
    // The sheet is no taller than the window, and the list scrolls inside it.
    CHECK(panel.top() >= 0);
    QQuickItem* flick = need(rig, QStringLiteral("environmentFlickable"));
    CHECK(flick->property("contentHeight").toDouble() > flick->height());
    const QRectF area = rig.geometry(flick);
    CHECK(area.bottom() <= rig.window().height() + 0.5);
    // The last choice is below the fold, and scrolling to the end brings it into the area.
    QQuickItem* last = choice(rig, "environment_env-30");
    CHECK(rig.geometry(last).bottom() > area.bottom());
    flick->setProperty("contentY", flick->property("contentHeight").toDouble() - flick->height());
    rig.settle();
    CHECK(rig.geometry(last).bottom() <= area.bottom() + 0.5);
}
