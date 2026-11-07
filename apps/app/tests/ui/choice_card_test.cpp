// SPDX-License-Identifier: GPL-3.0-or-later
// ChoiceCard (docs/plan/stage-4.md, 4.6): choice_card.select.
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

// The screen owns the selection: it sets `selected` on exactly one card.
constexpr auto CHOICES = R"(
    property int selectedIndex: 0
    Column {
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        spacing: Theme.space.sectionGap

        ChoiceCard {
            objectName: "changed"
            title: "It changed during the day"
            description: "For example an over-the-air update. Adds a timed entry to the log."
            selected: host.selectedIndex === 0
            onClicked: host.selectedIndex = 0
        }
        ChoiceCard {
            objectName: "typo"
            title: "I typed it wrong"
            description: "Corrects the value. Nothing is added to the log."
            selected: host.selectedIndex === 1
            onClicked: host.selectedIndex = 1
        }
    })";

int selected_count(UiRig& rig)
{
    int count = 0;
    for (const char* name : {"changed", "typo"})
    {
        count += rig.find(QString::fromLatin1(name))->property("selected").toBool() ? 1 : 0;
    }
    return count;
}

/// The thin rectangles of a card that are drawn rotated: the arms of the check mark, when it is shown.
int check_mark_arms(UiRig& rig, QQuickItem* card)
{
    int shown = 0;
    for (const Node& node : rig.tree(card))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) && node.m_Visible &&
            node.m_Item->rotation() != 0)
        {
            ++shown;
        }
    }
    return shown;
}

} // namespace

TEST_CASE("choice_card.select: exactly one card is selected, whichever is tapped", "[ui][choice_card]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHOICES)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* changed = rig.find(QStringLiteral("changed"));
    QQuickItem* typo = rig.find(QStringLiteral("typo"));
    REQUIRE(changed != nullptr);
    REQUIRE(typo != nullptr);
    QSignalSpy clicked(typo, SIGNAL(clicked()));

    CHECK(selected_count(rig) == 1);
    CHECK(changed->property("selected").toBool());
    rig.tap(typo);
    CHECK(clicked.count() == 1);
    CHECK(selected_count(rig) == 1);
    CHECK(typo->property("selected").toBool());
    rig.tap(typo);
    CHECK(selected_count(rig) == 1);
    rig.tap(changed);
    CHECK(changed->property("selected").toBool());
    CHECK(selected_count(rig) == 1);
}

TEST_CASE("choice_card.select: the selected state is a check mark and a thicker border, not colour alone",
          "[ui][choice_card]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHOICES)) != nullptr);
    QQuickItem* changed = rig.find(QStringLiteral("changed"));
    QQuickItem* typo = rig.find(QStringLiteral("typo"));
    REQUIRE(changed != nullptr);
    REQUIRE(typo != nullptr);

    CHECK(border_of(changed) == 2.0);
    CHECK(border_of(typo) == 1.0);
    // The two arms of the check mark are shown in the selected card and in no other.
    CHECK(check_mark_arms(rig, changed) == 2);
    CHECK(check_mark_arms(rig, typo) == 0);

    rig.tap(typo);
    CHECK(check_mark_arms(rig, changed) == 0);
    CHECK(check_mark_arms(rig, typo) == 2);
    CHECK(border_of(typo) == 2.0);
    CHECK(border_of(changed) == 1.0);
}

TEST_CASE("choice_card.voiceover: a radio button with its explanation as the description and its state",
          "[ui][choice_card]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHOICES)) != nullptr);
    QQuickItem* changed = rig.find(QStringLiteral("changed"));
    QQuickItem* typo = rig.find(QStringLiteral("typo"));
    REQUIRE(changed != nullptr);
    REQUIRE(typo != nullptr);
    QSignalSpy clicked(typo, SIGNAL(clicked()));

    QAccessibleInterface* first = QAccessible::queryAccessibleInterface(changed);
    QAccessibleInterface* second = QAccessible::queryAccessibleInterface(typo);
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->role() == QAccessible::RadioButton);
    CHECK(first->text(QAccessible::Name) == QStringLiteral("It changed during the day"));
    CHECK(first->text(QAccessible::Description).contains(QStringLiteral("timed entry")));
    CHECK(first->state().checkable);
    CHECK(first->state().checked);
    CHECK_FALSE(second->state().checked);

    second->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(clicked.count() == 1);
    CHECK(second->state().checked);
    CHECK_FALSE(first->state().checked);
}

TEST_CASE("choice_card.layout: the card grows with its text, which wraps and is never cut", "[ui][choice_card]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHOICES)) != nullptr);
    QQuickItem* changed = rig.find(QStringLiteral("changed"));
    REQUIRE(changed != nullptr);
    const double normal = rig.geometry(changed).height();
    CHECK(normal >= 44);
    rig.set_text_scale(3.1);
    CHECK(rig.geometry(changed).height() > 2 * normal);
    expect_clean(check_text_scale(rig));
}

TEST_CASE("choice_card.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][choice_card]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(CHOICES)) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
