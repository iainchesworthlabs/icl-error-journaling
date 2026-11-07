// SPDX-License-Identifier: GPL-3.0-or-later
// PrimaryButton (docs/plan/stage-4.md, 4.6): primary_button.states.
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QQuickItem>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::input;

namespace
{

constexpr auto BUTTON = R"(
    PrimaryButton {
        objectName: "button"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        text: "Start logging"
    })";

} // namespace

TEST_CASE("primary_button.states: default is the blue fill, pressed is the darker blue, and a release emits once",
          "[ui][primary_button]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(BUTTON)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* button = rig.find(QStringLiteral("button"));
    REQUIRE(button != nullptr);
    QSignalSpy clicked(button, SIGNAL(clicked()));

    CHECK(fill_of(button) == theme_colour(rig, "primary"));

    const int finger = rig.press(button);
    CHECK(clicked.count() == 0);
    CHECK(button->property("pressed").toBool());
    CHECK(fill_of(button) == theme_colour(rig, "primaryPressed"));
    rig.release(finger);
    CHECK(clicked.count() == 1);
    CHECK(fill_of(button) == theme_colour(rig, "primary"));

    rig.set_dark(true);
    CHECK(fill_of(button) == theme_colour(rig, "primary"));
    CHECK(theme_colour(rig, "primary") != QColor(QStringLiteral("#1E5B7A")));
}

TEST_CASE("primary_button.states: disabled is dimmed, reported as disabled and does not react", "[ui][primary_button]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(BUTTON)) != nullptr);
    QQuickItem* button = rig.find(QStringLiteral("button"));
    REQUIRE(button != nullptr);
    QSignalSpy clicked(button, SIGNAL(clicked()));

    button->setEnabled(false);
    rig.layout();
    CHECK(button->opacity() < 1.0);
    rig.tap(button);
    CHECK(clicked.count() == 0);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(button);
    REQUIRE(iface != nullptr);
    CHECK(iface->state().disabled);

    button->setEnabled(true);
    rig.layout();
    CHECK(button->opacity() == 1.0);
    rig.tap(button);
    CHECK(clicked.count() == 1);
}

TEST_CASE("primary_button.geometry: 54 pt high, 44 pt when compact, and taller with larger text",
          "[ui][primary_button]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(BUTTON)) != nullptr);
    QQuickItem* button = rig.find(QStringLiteral("button"));
    REQUIRE(button != nullptr);
    CHECK(rig.geometry(button).height() == 54);
    CHECK(rig.geometry(button).width() == 402 - 32);

    rig.set_text_scale(3.1);
    CHECK(rig.geometry(button).height() > 54);
    rig.set_text_scale(1.0);
    CHECK(rig.geometry(button).height() == 54);

    button->setProperty("compact", true);
    rig.layout();
    CHECK(rig.geometry(button).height() == 44);
}

TEST_CASE("primary_button.accessibility: the label is the name unless a screen gives one, and the press action works",
          "[ui][primary_button]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(BUTTON)) != nullptr);
    QQuickItem* button = rig.find(QStringLiteral("button"));
    REQUIRE(button != nullptr);
    QSignalSpy clicked(button, SIGNAL(clicked()));

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(button);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::Button);
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("Start logging"));
    press_action(button);
    CHECK(clicked.count() == 1);

    button->setProperty("accessibleName", QStringLiteral("Start logging for UNIT-27"));
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("Start logging for UNIT-27"));

    button->forceActiveFocus();
    rig.key(Qt::Key_Return);
    CHECK(clicked.count() == 2);
}

TEST_CASE("primary_button.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][primary_button]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(BUTTON)) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
