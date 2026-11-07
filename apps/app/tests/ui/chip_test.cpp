// SPDX-License-Identifier: GPL-3.0-or-later
// Chip (docs/plan/stage-4.md, 4.6): the template chip. The chip tests of `composer.chips` are in composer_test too.
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

constexpr auto CHIP = R"(
    Chip {
        objectName: "chip"
        x: Theme.space.screenMargin
        y: 100
        maxWidth: host.width - 2 * Theme.space.screenMargin
        text: "Played song"
    })";

} // namespace

TEST_CASE("chip.tap: a tap emits clicked once, on the release, and a touch that only presses emits nothing",
          "[ui][chip]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHIP)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* chip = rig.find(QStringLiteral("chip"));
    REQUIRE(chip != nullptr);
    QSignalSpy clicked(chip, SIGNAL(clicked()));

    const int finger = rig.press(chip);
    CHECK(clicked.count() == 0);
    CHECK(chip->property("pressed").toBool());
    rig.release(finger);
    CHECK(clicked.count() == 1);
    CHECK_FALSE(chip->property("pressed").toBool());

    rig.tap(chip);
    CHECK(clicked.count() == 2);
}

TEST_CASE("chip.geometry: 32 pt high with a 44 pt touch target, the name on one line", "[ui][chip]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHIP)) != nullptr);
    QQuickItem* chip = rig.find(QStringLiteral("chip"));
    REQUIRE(chip != nullptr);
    const QRectF rect = rig.geometry(chip);
    // The drawn chip is Theme.size.chipHeight; the font of the machine may make it a little taller.
    CHECK(rect.height() >= 32);
    CHECK(rect.height() < 44);
    CHECK(rect.width() > rect.height());
    expect_clean(check_targets(rig));
}

TEST_CASE("chip.wrap: a name wider than maxWidth wraps and grows the chip, and the name is never cut", "[ui][chip]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QStringLiteral(R"(
        Chip {
            objectName: "chip"
            x: Theme.space.screenMargin
            y: 100
            maxWidth: 140
            text: "Visitor interaction at the North Site gate"
        })")) != nullptr);
    QQuickItem* chip = rig.find(QStringLiteral("chip"));
    REQUIRE(chip != nullptr);
    CHECK(rig.geometry(chip).width() <= 140.5);
    CHECK(rig.geometry(chip).height() > 44);
    expect_clean(check_text_scale(rig));

    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_Item == chip)
        {
            CHECK(node.m_Name == QStringLiteral("Visitor interaction at the North Site gate"));
            CHECK(node.m_Role == QAccessible::Button);
        }
    }
}

TEST_CASE("chip.keys_and_voiceover: Return, Space and the accessibility press all emit clicked once", "[ui][chip]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHIP)) != nullptr);
    QQuickItem* chip = rig.find(QStringLiteral("chip"));
    REQUIRE(chip != nullptr);
    QSignalSpy clicked(chip, SIGNAL(clicked()));

    chip->forceActiveFocus();
    rig.key(Qt::Key_Return);
    CHECK(clicked.count() == 1);
    rig.key(Qt::Key_Space);
    CHECK(clicked.count() == 2);

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(chip);
    REQUIRE(iface != nullptr);
    QAccessibleActionInterface* actions = iface->actionInterface();
    REQUIRE(actions != nullptr);
    CHECK(actions->actionNames().contains(QAccessibleActionInterface::pressAction()));
    actions->doAction(QAccessibleActionInterface::pressAction());
    CHECK(clicked.count() == 3);
}

TEST_CASE("chip.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][chip]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHIP)) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
