// SPDX-License-Identifier: GPL-3.0-or-later
// NavBar (docs/plan/stage-4.md, 4.4): the compact and the large shape, the actions, the title that moves to its own
// line, accessibility and the cross-cutting checks.
#include "container_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAccessible>
#include <QFont>
#include <QQuickItem>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::containers;

namespace
{

QQuickItem* load_nav(UiRig& rig, const QVariantMap& properties)
{
    QQuickItem* nav = rig.load(QStringLiteral("NavBar"), properties);
    INFO(rig.errors().toStdString());
    REQUIRE(nav != nullptr);
    return nav;
}

QVariantMap edit_sheet()
{
    return {{QStringLiteral("title"), QStringLiteral("Edit")},
            {QStringLiteral("leadingText"), QStringLiteral("Cancel")},
            {QStringLiteral("trailingText"), QStringLiteral("Save")}};
}

} // namespace

TEST_CASE("nav_bar: the compact shape has the actions at the ends and the title between them", "[ui][nav_bar]")
{
    UiRig rig;
    // A short title: the width of text depends on the font, and the runner's is wider than the phone's.
    QQuickItem* nav = load_nav(rig, edit_sheet());

    const QRectF leading = rig.geometry(rig.find(QStringLiteral("navLeading")));
    const QRectF trailing = rig.geometry(rig.find(QStringLiteral("navTrailing")));
    const QRectF title = rig.geometry(rig.find(QStringLiteral("navTitle")));

    CHECK(leading.left() == 16);
    CHECK(trailing.right() == 402 - 16);
    CHECK(leading.height() >= 44);
    CHECK(trailing.height() >= 44);
    CHECK(leading.width() >= 44);
    CHECK(trailing.width() >= 44);
    // One row of 44 pt, with the title centred in the window and on the row.
    CHECK(nav->property("implicitHeight").toDouble() == 44);
    CHECK(title.center().x() == 201);
    CHECK(title.center().y() == 22);
    CHECK(title.left() > leading.right());
    CHECK(title.right() < trailing.left());
    CHECK_FALSE(nav->property("stacked").toBool());
    CHECK_FALSE(rig.find(QStringLiteral("navTitleBelow"))->isVisible());
}

TEST_CASE("nav_bar: the actions emit once and the trailing one can be disabled", "[ui][nav_bar]")
{
    UiRig rig;
    QQuickItem* nav = load_nav(rig, edit_sheet());
    QSignalSpy leading(nav, SIGNAL(leadingTriggered()));
    QSignalSpy trailing(nav, SIGNAL(trailingTriggered()));

    rig.tap(rig.find(QStringLiteral("navLeading")));
    CHECK(leading.count() == 1);
    CHECK(trailing.count() == 0);
    rig.tap(rig.find(QStringLiteral("navTrailing")));
    CHECK(trailing.count() == 1);

    SECTION("a screen reader's activation")
    {
        for (const char* name : {"navLeading", "navTrailing"})
        {
            QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(rig.find(QString::fromLatin1(name)));
            REQUIRE(iface != nullptr);
            iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        }
        CHECK(leading.count() == 2);
        CHECK(trailing.count() == 2);
    }

    SECTION("Save is off until the page can be saved")
    {
        nav->setProperty("trailingEnabled", false);
        rig.layout();
        rig.tap(rig.find(QStringLiteral("navTrailing")));
        CHECK(trailing.count() == 1);
        for (const Node& node : rig.accessible_tree())
        {
            if (node.m_ObjectName == QStringLiteral("navTrailing"))
            {
                CHECK_FALSE(node.m_Enabled);
            }
        }
    }

    SECTION("an action with no text is not there")
    {
        nav->setProperty("leadingText", QString());
        rig.layout();
        CHECK_FALSE(rig.find(QStringLiteral("navLeading"))->isVisible());
        rig.tap(rig.find(QStringLiteral("navLeading")), QPointF(30, 20));
        CHECK(leading.count() == 1);
    }
}

TEST_CASE("nav_bar: a back chevron widens the leading action, and the accessible name can say more than the text",
          "[ui][nav_bar]")
{
    UiRig rig;
    QQuickItem* nav = load_nav(rig, {{QStringLiteral("title"), QStringLiteral("Thu 5 Nov")},
                                     {QStringLiteral("leadingText"), QStringLiteral("History")},
                                     {QStringLiteral("leadingAccessibleName"), QStringLiteral("Back to History")},
                                     {QStringLiteral("large"), true}});
    const double plain = rig.geometry(rig.find(QStringLiteral("navLeading"))).width();
    nav->setProperty("leadingBack", true);
    rig.layout();
    CHECK(rig.geometry(rig.find(QStringLiteral("navLeading"))).width() > plain);

    const std::vector<Node> nodes = rig.accessible_tree();
    const Node* leading = find_node(nodes, QStringLiteral("navLeading"));
    REQUIRE(leading != nullptr);
    CHECK(leading->m_Name == QStringLiteral("Back to History"));
    CHECK(leading->m_Role == QAccessible::Button);
}

TEST_CASE("nav_bar: the large shape puts the 30 pt title under the actions", "[ui][nav_bar]")
{
    UiRig rig;
    QQuickItem* nav = load_nav(rig, {{QStringLiteral("title"), QStringLiteral("Today's log")},
                                     {QStringLiteral("trailingText"), QStringLiteral("Share")},
                                     {QStringLiteral("large"), true}});
    QQuickItem* title = rig.find(QStringLiteral("navTitleBelow"));
    REQUIRE(title->isVisible());
    CHECK_FALSE(rig.find(QStringLiteral("navTitle"))->isVisible());
    CHECK(title->property("font").value<QFont>().pixelSize() == 30);
    CHECK(title->property("font").value<QFont>().weight() == QFont::Bold);
    CHECK(rig.geometry(title).left() == 16);
    CHECK(rig.geometry(title).top() == 44 + 8); // 8 pt under the row of actions
    CHECK(nav->property("implicitHeight").toDouble() == rig.geometry(title).bottom() + 8); // and 8 pt under the title

    SECTION("with no action there is no row, and the title is at the top")
    {
        nav->setProperty("trailingText", QString());
        rig.layout();
        CHECK(rig.geometry(title).top() == 0);
        CHECK(nav->property("rowHeight").toDouble() == 0);
    }
}

TEST_CASE("nav_bar.collapsed: a large bar whose page has scrolled shows the compact title and a divider",
          "[ui][nav_bar]")
{
    UiRig rig;
    QQuickItem* nav = load_nav(rig, {{QStringLiteral("title"), QStringLiteral("Today's details")},
                                     {QStringLiteral("leadingText"), QStringLiteral("Log")},
                                     {QStringLiteral("leadingBack"), true},
                                     {QStringLiteral("large"), true}});
    CHECK_FALSE(nav->property("collapsed").toBool());
    CHECK_FALSE(rig.find(QStringLiteral("navDivider"))->isVisible());

    nav->setProperty("scrollPosition", 40);
    rig.layout();
    CHECK(nav->property("collapsed").toBool());
    QQuickItem* inline_title = rig.find(QStringLiteral("navTitle"));
    REQUIRE(inline_title != nullptr);
    CHECK(inline_title->isVisible());
    CHECK(inline_title->property("font").value<QFont>().weight() == QFont::DemiBold);
    CHECK(rig.geometry(inline_title).center().x() == 201);
    CHECK_FALSE(rig.find(QStringLiteral("navTitleBelow"))->isVisible());
    QQuickItem* divider = rig.find(QStringLiteral("navDivider"));
    REQUIRE(divider != nullptr);
    CHECK(divider->isVisible());
    CHECK(rig.geometry(divider).top() == 44);
    CHECK(rig.geometry(divider).height() == 1);
    CHECK(nav->property("implicitHeight").toDouble() == 45);

    nav->setProperty("scrollPosition", 0);
    rig.layout();
    CHECK_FALSE(nav->property("collapsed").toBool());
    CHECK(rig.find(QStringLiteral("navTitleBelow"))->isVisible());
}

TEST_CASE("nav_bar.compact: a title that fits is on one line, and a sheet's bar has 8 pt above and 16 pt below",
          "[ui][nav_bar]")
{
    UiRig rig;
    QQuickItem* nav = load_nav(rig, {{QStringLiteral("title"), QStringLiteral("Time")},
                                     {QStringLiteral("leadingText"), QStringLiteral("Cancel")},
                                     {QStringLiteral("trailingText"), QStringLiteral("Done")},
                                     {QStringLiteral("inSheet"), true}});
    rig.layout();
    INFO("width " << nav->width() << ", title " << nav->property("titleWidth").toDouble() << ", room "
                  << nav->property("inlineRoom").toDouble());
    QQuickItem* title = rig.find(QStringLiteral("navTitle"));
    REQUIRE(title != nullptr);
    REQUIRE(title->isVisible());
    CHECK(title->property("lineCount").toInt() == 1);
    CHECK(rig.geometry(rig.find(QStringLiteral("navLeading"))).top() == 8);
    CHECK(nav->property("implicitHeight").toDouble() == 8 + 44 + 16);
    // The back chevron is 20 pt, 2 pt from its label.
    nav->setProperty("leadingBack", true);
    rig.layout();
    const QRectF chevron = rig.geometry(rig.find(QStringLiteral("navBackChevron")));
    CHECK(chevron.width() == 20);
    CHECK(rig.geometry(rig.find(QStringLiteral("navLeadingLabel"))).left() == chevron.right() + 2);
}

TEST_CASE("nav_bar: a compact title that does not fit between the actions goes to a line of its own", "[ui][nav_bar]")
{
    UiRig rig;
    QQuickItem* nav =
        load_nav(rig, {{QStringLiteral("title"), QStringLiteral("Software version, changed during the day")},
                       {QStringLiteral("leadingText"), QStringLiteral("Cancel")},
                       {QStringLiteral("trailingText"), QStringLiteral("Save")}});

    // At the default size even that long title fits on the row only when it is short enough.
    rig.set_text_scale(3.1);
    CHECK(nav->property("stacked").toBool());
    QQuickItem* title = rig.find(QStringLiteral("navTitleBelow"));
    REQUIRE(title->isVisible());
    CHECK_FALSE(rig.find(QStringLiteral("navTitle"))->isVisible());
    // The title wraps within the window and is read after the two actions, as it appears.
    CHECK(rig.geometry(title).left() >= 0);
    CHECK(rig.geometry(title).right() <= 402);
    CHECK(rig.geometry(title).top() >= rig.geometry(rig.find(QStringLiteral("navLeading"))).bottom());
    expect_clean(check_everything(rig));
}

TEST_CASE("nav_bar: the title is a heading, and the actions are named buttons", "[ui][nav_bar]")
{
    UiRig rig;
    load_nav(rig, edit_sheet());
    const std::vector<Node> nodes = rig.accessible_tree();
    const Node* title = find_node(nodes, QStringLiteral("navTitle"));
    REQUIRE(title != nullptr);
    CHECK(title->m_Role == QAccessible::Heading);
    CHECK(title->m_Name == QStringLiteral("Edit"));
    const Node* leading = find_node(nodes, QStringLiteral("navLeading"));
    REQUIRE(leading != nullptr);
    CHECK(leading->m_Role == QAccessible::Button);
    CHECK(leading->m_Name == QStringLiteral("Cancel"));
    const Node* trailing = find_node(nodes, QStringLiteral("navTrailing"));
    REQUIRE(trailing != nullptr);
    CHECK(trailing->m_Name == QStringLiteral("Save"));
    // Only the title that is shown is there.
    CHECK(find_shown(nodes, QStringLiteral("navTitleBelow")) == nullptr);
}

TEST_CASE("nav_bar: passes the cross-cutting checks in each shape, in light and dark", "[ui][nav_bar]")
{
    UiRig rig;
    const bool large = GENERATE(false, true);
    const bool dark = GENERATE(false, true);
    QVariantMap properties = edit_sheet();
    properties.insert(QStringLiteral("large"), large);
    properties.insert(QStringLiteral("leadingBack"), true);
    INFO("large " << large << ", dark " << dark);
    load_nav(rig, properties);
    rig.set_dark(dark);
    expect_clean(check_everything(rig));
}

TEST_CASE("motion.reduce: the title bar has nothing that moves", "[ui][nav_bar][motion]")
{
    UiRig rig;
    load_nav(rig, edit_sheet());
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
