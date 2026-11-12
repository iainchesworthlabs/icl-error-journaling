// SPDX-License-Identifier: GPL-3.0-or-later
// HelpButton (Stage 10, 10.6): the question mark in a 44 pt target, its accessible name, its signal, and its place in
// the title bar of NavBar.
#include "container_support.hpp"
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::input;
using ej::ui::containers::find_node;
using ej::ui::containers::find_shown;

TEST_CASE("help.button: a 44 pt target named for its topic, which emits once for a tap and for a screen reader's press",
          "[ui][help][help_button]")
{
    UiRig rig;
    QQuickItem* host = load_host(rig, QStringLiteral(R"(
        HelpButton { objectName: "button"; accessibleName: "Help with error types" })"));
    INFO(rig.errors().toStdString());
    REQUIRE(host != nullptr);
    QQuickItem* button = require_item(rig, QStringLiteral("button"));
    QSignalSpy triggered(button, SIGNAL(triggered()));

    CHECK(button->width() >= 44.0);
    CHECK(button->height() >= 44.0);

    const std::vector<Node> tree = rig.accessible_tree();
    const Node* node = find_node(tree, QStringLiteral("button"));
    REQUIRE(node != nullptr);
    CHECK(node->m_Name == QStringLiteral("Help with error types"));
    CHECK(node->m_Interactive);

    rig.tap(button);
    CHECK(triggered.count() == 1);
    press_action(button);
    CHECK(triggered.count() == 2);

    // The icon is the question mark, drawn at 24 pt, and a screen reader skips it.
    QQuickItem* icon = require_item(rig, QStringLiteral("helpIcon"));
    CHECK(icon->property("kind").toString() == QStringLiteral("help"));
    CHECK(icon->width() == 24.0);
    CHECK(icon->property("known").toBool());
}

TEST_CASE("help.button: in a title bar it sits before the trailing action, and a bar with no help name has none",
          "[ui][help][help_button]")
{
    UiRig rig;
    QQuickItem* host = load_host(rig, QStringLiteral(R"(
        NavBar {
            objectName: "bar"
            width: parent.width
            large: true
            title: "Error types"
            leadingText: "Settings"
            leadingBack: true
            trailingText: "Done"
            helpName: "Help with error types"
        })"));
    INFO(rig.errors().toStdString());
    REQUIRE(host != nullptr);
    QQuickItem* bar = require_item(rig, QStringLiteral("bar"));
    QQuickItem* help = require_item(rig, QStringLiteral("navHelp"), bar);
    QQuickItem* trailing = require_item(rig, QStringLiteral("navTrailing"), bar);
    QSignalSpy asked(bar, SIGNAL(helpTriggered()));
    QSignalSpy done(bar, SIGNAL(trailingTriggered()));
    rig.layout();

    REQUIRE(help->isVisible());
    CHECK(help->width() >= 44.0);
    CHECK(rig.geometry(help).right() <= rig.geometry(trailing).left() + 0.5);
    CHECK(rig.geometry(help).center().y() == rig.geometry(trailing).center().y());
    rig.tap(help);
    CHECK(asked.count() == 1);
    CHECK(done.count() == 0);
    rig.tap(trailing);
    CHECK(done.count() == 1);
    CHECK(asked.count() == 1);

    SECTION("with no trailing action it is at the trailing edge")
    {
        bar->setProperty("trailingText", QString());
        rig.layout();
        CHECK(rig.geometry(help).right() == rig.geometry(bar).right() - bar->property("sideInset").toDouble());
    }

    SECTION("with no help name there is no button")
    {
        bar->setProperty("helpName", QString());
        rig.layout();
        CHECK_FALSE(help->isVisible());
        const std::vector<Node> reached = rig.accessible_tree();
        CHECK(find_shown(reached, QStringLiteral("navHelp")) == nullptr);
    }
}
