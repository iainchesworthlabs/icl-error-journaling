// SPDX-License-Identifier: GPL-3.0-or-later
// GroupedList (docs/plan/stage-4.md, 4.6): the 14 pt group of rows under its heading.
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QQuickItem>

using namespace ej::ui;
using namespace ej::ui::input;

namespace
{

constexpr auto GROUP = R"(
    GroupedList {
        objectName: "group"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        title: "Location"
        footer: "Choose where the log was made."

        ListRow { objectName: "a"; text: "North Site"; checkable: true; checked: true }
        ListRow { objectName: "b"; text: "South Site"; checkable: true }
        ListRow { objectName: "c"; text: "Other" }
    })";

QQuickItem* text_item(UiRig& rig, const QString& text)
{
    for (const Node& node : rig.tree())
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) &&
            node.m_Item->property("text").toString() == text)
        {
            return node.m_Item;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("grouped_list.layout: the rows are stacked at the width of the group inside 14 pt corners",
          "[ui][grouped_list]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(GROUP)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* group = rig.find(QStringLiteral("group"));
    QQuickItem* a = rig.find(QStringLiteral("a"));
    QQuickItem* b = rig.find(QStringLiteral("b"));
    QQuickItem* c = rig.find(QStringLiteral("c"));
    REQUIRE(group != nullptr);
    REQUIRE(a != nullptr);

    CHECK(rig.geometry(a).width() == rig.geometry(group).width());
    CHECK(rig.geometry(b).top() == rig.geometry(a).bottom());
    CHECK(rig.geometry(c).top() == rig.geometry(b).bottom());
    CHECK(rig.geometry(c).height() == 48);

    // The white card behind the rows has the radius of the group token.
    bool found = false;
    for (const Node& node : rig.tree(group))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) && node.m_Rect.height() == 3 * 48 &&
            node.m_Rect.width() == rig.geometry(group).width())
        {
            found = true;
            CHECK(node.m_Item->property("radius").toDouble() == 14);
            CHECK(node.m_Item->property("color").value<QColor>() == theme_colour(rig, "surface"));
        }
    }
    CHECK(found);
}

TEST_CASE("grouped_list.heading: the title is in capitals above the group and the note is under it",
          "[ui][grouped_list]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(GROUP)) != nullptr);
    QQuickItem* group = rig.find(QStringLiteral("group"));
    QQuickItem* a = rig.find(QStringLiteral("a"));
    QQuickItem* c = rig.find(QStringLiteral("c"));
    QQuickItem* heading = text_item(rig, QStringLiteral("LOCATION"));
    QQuickItem* note = text_item(rig, QStringLiteral("Choose where the log was made."));
    REQUIRE(group != nullptr);
    REQUIRE(heading != nullptr);
    REQUIRE(note != nullptr);

    CHECK(rig.geometry(heading).bottom() <= rig.geometry(a).top());
    CHECK(rig.geometry(note).top() >= rig.geometry(c).bottom());
    CHECK(rig.geometry(group).bottom() >= rig.geometry(note).bottom());

    group->setProperty("title", QString());
    group->setProperty("footer", QString());
    rig.layout();
    CHECK(rig.geometry(group).height() == 3 * 48);
    CHECK(rig.geometry(a).top() == rig.geometry(group).top());
}

TEST_CASE("grouped_list.voiceover: the group is named by its title and holds its rows in order", "[ui][grouped_list]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(GROUP)) != nullptr);
    QQuickItem* group = rig.find(QStringLiteral("group"));
    REQUIRE(group != nullptr);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(group);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::Grouping);
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("Location"));

    QStringList names;
    for (const Node& node : rig.accessible_tree(group))
    {
        if (node.m_Role == QAccessible::Button)
        {
            names.push_back(node.m_Name);
        }
    }
    CHECK(names == QStringList{QStringLiteral("North Site"), QStringLiteral("South Site"), QStringLiteral("Other")});
}

TEST_CASE("grouped_list.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][grouped_list]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(GROUP)) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
