// SPDX-License-Identifier: GPL-3.0-or-later
// HeaderCard (docs/plan/stage-4.md, 4.6): header_card.fields and header_card.skeleton.
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

constexpr auto FULL = R"(
    HeaderCard {
        objectName: "card"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        unitText: "UNIT-27"
        locationText: "North Site"
        versionText: "1.4.2"
        dateText: "Sat 7 Nov"
        facilitatorText: "Sam Example"
        missingFields: []
    })";

constexpr auto EMPTY = R"(
    HeaderCard {
        objectName: "card"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        unitText: ""
        locationText: ""
        versionText: ""
        dateText: "Sat 7 Nov"
        facilitatorText: "Sam Example"
        missingFields: ["unit_number", "location", "software_version"]
    })";

/// The text of every Text under the card that is shown.
QStringList shown_texts(UiRig& rig, QQuickItem* card)
{
    QStringList texts;
    for (const Node& node : rig.tree(card))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) && node.m_Visible)
        {
            texts.push_back(node.m_Item->property("text").toString());
        }
    }
    return texts;
}

QQuickItem* card_of(UiRig& rig, const char* source)
{
    REQUIRE(load_host(rig, QString::fromUtf8(source)) != nullptr);
    QQuickItem* card = rig.find(QStringLiteral("card"));
    REQUIRE(card != nullptr);
    return card;
}

} // namespace

TEST_CASE("header_card.fields: the five values are shown on the plum card", "[ui][header_card]")
{
    UiRig rig;
    QQuickItem* card = card_of(rig, FULL);
    INFO(rig.errors().toStdString());

    const QStringList texts = shown_texts(rig, card);
    CHECK(texts.contains(QStringLiteral("UNIT-27")));
    CHECK(texts.contains(QStringLiteral("North Site · v1.4.2")));
    CHECK(texts.contains(QStringLiteral("Sat 7 Nov")));
    CHECK(texts.contains(QStringLiteral("Sam Example")));
    CHECK(texts.size() == 4);
    CHECK(card->property("missingCount").toInt() == 0);

    CHECK(fill_of(card) == theme_colour(rig, "headerCard"));
    CHECK(first_rectangle(card)->property("radius").toDouble() == 20);
    CHECK(rig.geometry(card).width() == 402 - 32);
    // 64 pt in the design: 20 pt and 13 pt lines with 12 pt above and below.
    CHECK(rig.geometry(card).height() >= 64);
    CHECK(rig.geometry(card).height() < 80);

    rig.set_dark(true);
    CHECK(fill_of(card) == theme_colour(rig, "headerCard"));
}

TEST_CASE("header_card.fields: a missing field reads as a prompt and not as a gap, and the strip names what is missing",
          "[ui][header_card]")
{
    // Only one rig lives at a time, so the height of the full card is taken first.
    double full_height = 0;
    {
        UiRig other;
        full_height = other.geometry(card_of(other, FULL)).height();
    }
    UiRig rig;
    QQuickItem* card = card_of(rig, EMPTY);

    const QStringList texts = shown_texts(rig, card);
    CHECK(texts.contains(QStringLiteral("Unit —")));
    CHECK(texts.contains(QStringLiteral("Location not set · v —")));
    CHECK(texts.contains(QStringLiteral("Not filled in yet: UNIT number, Location, Version")));
    for (const QString& text : texts)
    {
        CHECK_FALSE(text.trimmed().isEmpty());
    }
    CHECK(card->property("missingCount").toInt() == 3);

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(card);
    REQUIRE(iface != nullptr);
    const QString name = iface->text(QAccessible::Name);
    CHECK(name.contains(QStringLiteral("Unit not set")));
    CHECK(name.contains(QStringLiteral("Not filled in yet: UNIT number, Location, Version")));

    // The strip is inside the card, so the card is taller than when nothing is missing.
    CHECK(rig.geometry(card).height() > full_height);
}

TEST_CASE("header_card.fields: filling a field changes the prompt, the strip and the name", "[ui][header_card]")
{
    UiRig rig;
    QQuickItem* card = card_of(rig, EMPTY);
    card->setProperty("unitText", QStringLiteral("UNIT-27"));
    card->setProperty("missingFields", QStringList{QStringLiteral("location"), QStringLiteral("software_version")});
    rig.layout();

    const QStringList texts = shown_texts(rig, card);
    CHECK(texts.contains(QStringLiteral("UNIT-27")));
    CHECK(texts.contains(QStringLiteral("Not filled in yet: Location, Version")));
    CHECK(card->property("missingCount").toInt() == 2);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(card);
    REQUIRE(iface != nullptr);
    CHECK(iface->text(QAccessible::Name).contains(QStringLiteral("Unit UNIT-27")));

    card->setProperty("locationText", QStringLiteral("North Site"));
    card->setProperty("versionText", QStringLiteral("1.4.2"));
    card->setProperty("missingFields", QStringList());
    rig.layout();
    const QStringList filled = shown_texts(rig, card);
    CHECK(filled.contains(QStringLiteral("North Site · v1.4.2")));
    CHECK(filled.size() == 4);
    CHECK_FALSE(iface->text(QAccessible::Name).contains(QStringLiteral("Not filled in yet")));
}

TEST_CASE("header_card.tap: a tap emits clicked once, the accessibility press and Return do too", "[ui][header_card]")
{
    UiRig rig;
    QQuickItem* card = card_of(rig, FULL);
    QSignalSpy clicked(card, SIGNAL(clicked()));

    const int finger = rig.press(card);
    CHECK(clicked.count() == 0);
    rig.release(finger);
    CHECK(clicked.count() == 1);

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(card);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::Button);
    iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(clicked.count() == 2);

    card->forceActiveFocus();
    rig.key(Qt::Key_Return);
    CHECK(clicked.count() == 3);
}

TEST_CASE("header_card.skeleton: shapes only, in neutral colours, with the same height as the card",
          "[ui][header_card]")
{
    UiRig rig;
    QQuickItem* card = card_of(rig, FULL);
    const double height = rig.geometry(card).height();
    const QRectF rect = rig.geometry(card);

    card->setProperty("skeleton", true);
    rig.layout();
    CHECK(rig.geometry(card).height() == height);
    CHECK(rig.geometry(card) == rect);
    CHECK(fill_of(card) == theme_colour(rig, "swatch"));
    CHECK(shown_texts(rig, card).isEmpty());
    CHECK(card->property("accessibleName").toString().isEmpty());

    // It is not a control while it is a skeleton: no tap, no accessibility.
    QSignalSpy clicked(card, SIGNAL(clicked()));
    rig.tap(card);
    CHECK(clicked.count() == 0);
    for (const Node& node : rig.tree())
    {
        if (node.m_Item == card)
        {
            CHECK_FALSE(node.m_InAccessibleTree);
        }
    }

    // No type colour and no plum: every rectangle is one of the neutral swatch and border colours.
    for (const Node& node : rig.tree(card))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) && node.m_Visible)
        {
            const QColor colour = node.m_Item->property("color").value<QColor>();
            CHECK((colour == theme_colour(rig, "swatch") || colour == theme_colour(rig, "border")));
        }
    }
}

TEST_CASE("header_card.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][header_card]")
{
    for (const char* source : {FULL, EMPTY})
    {
        UiRig rig;
        card_of(rig, source);
        expect_clean(check_everything(rig));
        rig.set_dark(true);
        expect_clean(check_everything(rig));
        rig.set_reduce_motion(true);
        expect_clean(check_motion_reduced(rig));
    }
}
