// SPDX-License-Identifier: GPL-3.0-or-later
// FirstRunTip (Stage 10, 10.6): the card of the Log screen that says how to log, add a note and undo. Short enough to
// take the Last bar's place, readable in both schemes, with a Close button that emits and nothing else.
#include "container_support.hpp"
#include "rig/color_math.hpp"
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

const QString TITLE = QStringLiteral("Three things to know");
const QString LINES = QStringLiteral("Double-tap a tile to log it.\nHold a tile to log it with a note.\n"
                                     "Undo is on the message that appears.");

QQuickItem* load_tip(UiRig& rig)
{
    QQuickItem* tip =
        rig.load(QStringLiteral("FirstRunTip"), {{QStringLiteral("title"), TITLE}, {QStringLiteral("message"), LINES}});
    INFO(rig.errors().toStdString());
    REQUIRE(tip != nullptr);
    return tip;
}

} // namespace

TEST_CASE("help.first_run_tip_card: three lines under a title are under 100 pt, so the board keeps its tiles",
          "[ui][help][first_run_tip]")
{
    UiRig rig;
    QQuickItem* tip = load_tip(rig);
    rig.layout();

    QQuickItem* message = rig.find(QStringLiteral("tipMessage"));
    REQUIRE(message != nullptr);
    CAPTURE(message->width(), message->property("contentWidth").toDouble(),
            rig.geometry(rig.find(QStringLiteral("tipCard"))).width(),
            message->property("font").value<QFont>().pixelSize(),
            message->property("font").value<QFont>().pointSizeF());
    CHECK(message->property("lineCount").toInt() == 3);
    CHECK(rig.geometry(rig.find(QStringLiteral("tipTitle"))).height() < 30);
    // The card is its implicit height, which the Log screen reads, and 8 pt of padding, the title and three lines is
    // under 100.
    CHECK(tip->property("implicitHeight").toDouble() == rig.geometry(rig.find(QStringLiteral("tipCard"))).height());
    CHECK(tip->property("implicitHeight").toDouble() < 100.0);
    CHECK(tip->property("implicitHeight").toDouble() >= 44.0);

    // The text is clear of the icon and of the Close button.
    const QRectF card = rig.geometry(rig.find(QStringLiteral("tipCard")));
    const QRectF icon = rig.geometry(rig.find(QStringLiteral("tipIcon")));
    const QRectF close = rig.geometry(rig.find(QStringLiteral("tipClose")));
    const QRectF title = rig.geometry(rig.find(QStringLiteral("tipTitle")));
    const QRectF text = rig.geometry(message);
    CHECK(title.left() >= icon.right());
    CHECK(title.right() <= close.left());
    // The lines under the title run beneath the corner of the button, which is why they have the room.
    CHECK(text.right() <= card.right());
    CHECK(text.top() >= title.bottom());
    CHECK(text.bottom() <= card.bottom());
    CHECK(close.width() == 44.0);
    CHECK(close.height() == 44.0);
}

TEST_CASE("help.first_run_tip_card: Close emits once and the card does not hide itself; a screen reader can press it",
          "[ui][help][first_run_tip]")
{
    UiRig rig;
    QQuickItem* tip = load_tip(rig);
    QSignalSpy close(tip, SIGNAL(closeRequested()));

    rig.tap(rig.find(QStringLiteral("tipClose")));
    CHECK(close.count() == 1);
    CHECK(tip->isVisible());

    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(rig.find(QStringLiteral("tipClose")));
    REQUIRE(iface != nullptr);
    iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(close.count() == 2);

    const std::vector<Node> reached = rig.accessible_tree();
    const Node* button = find_node(reached, QStringLiteral("tipClose"));
    REQUIRE(button != nullptr);
    CHECK(button->m_Name == QStringLiteral("Close"));
    const Node* card = find_node(reached, QStringLiteral("tipCard"));
    REQUIRE(card != nullptr);
    CHECK(card->m_Name == TITLE);
    CHECK(card->m_Description == LINES);
}

TEST_CASE("help.first_run_tip_card: the words pass their contrast ratios in light and dark",
          "[ui][help][first_run_tip][contrast]")
{
    UiRig rig;
    load_tip(rig);
    const bool dark = GENERATE(false, true);
    INFO("dark " << dark);
    rig.set_dark(dark);
    rig.layout();

    const QColor fill = theme_colour(rig, "primaryTint");
    CHECK(contrast_ratio(theme_colour(rig, "textPrimary"), fill) >= 4.5);
    CHECK(contrast_ratio(theme_colour(rig, "primaryText"), fill) >= 3.0);
    CHECK(contrast_ratio(theme_colour(rig, "textSecondary"), fill) >= 3.0);
}

TEST_CASE("help.first_run_tip_card: the cross-cutting checks", "[ui][help][first_run_tip]")
{
    UiRig rig;
    load_tip(rig);
    rig.layout();
    expect_clean(check_names(rig));
    expect_clean(check_targets(rig));
    expect_clean(check_reading_order(rig));
    expect_clean(check_motion_reduced(rig));
}
