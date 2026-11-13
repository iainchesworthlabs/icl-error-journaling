// SPDX-License-Identifier: GPL-3.0-or-later
// LastEntryBar (docs/plan/stage-4.md, 4.5): last_bar.line, the empty state, the tap that opens Review, the skeleton and
// the accessibility checks.
#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cmath>

using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

QQuickItem* load_bar(UiRig& rig, const QString& line, int count, bool skeleton = false)
{
    QQuickItem* bar = rig.load(QStringLiteral("LastEntryBar"), {{QStringLiteral("lastLine"), line},
                                                                {QStringLiteral("entryCount"), count},
                                                                {QStringLiteral("skeleton"), skeleton}});
    REQUIRE(bar != nullptr);
    // The width the Log screen gives it; the rig sizes a loaded item to the window.
    bar->setWidth(370);
    bar->setHeight(bar->implicitHeight());
    rig.layout();
    return bar;
}

QString text_of(UiRig& rig, const char* name)
{
    QQuickItem* item = rig.find(QString::fromLatin1(name));
    REQUIRE(item != nullptr);
    return item->property("text").toString();
}

} // namespace

TEST_CASE("last_bar.line: shows the latest line and the count of today", "[ui][last_bar]")
{
    UiRig rig;
    QQuickItem* bar = load_bar(rig, QStringLiteral("10:20  T| Mute Delay"), 7);

    CHECK(text_of(rig, "lastBarLine") == QStringLiteral("10:20  T| Mute Delay"));
    CHECK(text_of(rig, "lastBarCount") == QStringLiteral("7 today"));
    CHECK(text_of(rig, "lastBarCaption") == QStringLiteral("LAST"));
    CHECK(require_item(rig, QStringLiteral("lastBarCount"))->isVisible());
    CHECK(rig.geometry(bar).width() == 370);
    CHECK(rig.geometry(bar).height() == 44);

    // It follows the model: another line and another count.
    bar->setProperty("lastLine", QStringLiteral("10:21  Sam Example waved to UNIT-27"));
    bar->setProperty("entryCount", 8);
    rig.layout();
    CHECK(text_of(rig, "lastBarLine") == QStringLiteral("10:21  Sam Example waved to UNIT-27"));
    CHECK(text_of(rig, "lastBarCount") == QStringLiteral("8 today"));

    // The name has the line and the count whole.
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_Item == bar)
        {
            CHECK(node.m_Name ==
                  QStringLiteral("Last entry: 10:21  Sam Example waved to UNIT-27, 8 today"));
            CHECK(node.m_Description == QStringLiteral("Opens Review"));
        }
    }
}

TEST_CASE("last_bar.line: with no entry it says so", "[ui][last_bar]")
{
    UiRig rig;
    QQuickItem* bar = load_bar(rig, QString(), 0);
    CHECK(text_of(rig, "lastBarLine") == QStringLiteral("No entries yet"));
    // The count is shown at zero too, as the LogEmptyBoard mock-up has it.
    CHECK(text_of(rig, "lastBarCount") == QStringLiteral("0 today"));
    CHECK(require_item(rig, QStringLiteral("lastBarCount"))->isVisible());

    bool named = false;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_Item == bar)
        {
            named = true;
            CHECK(node.m_Name == QStringLiteral("Last entry: none yet"));
        }
    }
    CHECK(named);
    expect_clean(check_names(rig));
}

TEST_CASE("last_bar.line: on one row the caption sits on the line's baseline", "[ui][last_bar]")
{
    for (const int count : {0, 7})
    {
        UiRig rig;
        load_bar(rig, count > 0 ? QStringLiteral("10:20  T| Mute Delay") : QString(), count);
        QQuickItem* caption = require_item(rig, QStringLiteral("lastBarCaption"));
        QQuickItem* line = require_item(rig, QStringLiteral("lastBarLine"));
        const double caption_baseline = rig.geometry(caption).top() + caption->baselineOffset();
        const double line_baseline = rig.geometry(line).top() + line->baselineOffset();
        CHECK(std::abs(caption_baseline - line_baseline) < 0.5);
    }
}

TEST_CASE("last_bar.line: a long line is cut on the row, and the name keeps it whole", "[ui][last_bar]")
{
    UiRig rig;
    const QString line = QStringLiteral("10:24  Sam Example reported that the downstream ticket indexer "
                                        "showed the entry twice on UNIT-27 at North Site");
    QQuickItem* bar = load_bar(rig, line, 12);
    QQuickItem* text = rig.find(QStringLiteral("lastBarLine"));
    CHECK(text->property("elide").toInt() == 1); // Text.ElideRight, as in the design
    CHECK(text->property("lineCount").toInt() == 1);
    CHECK(text->width() < text->property("implicitWidth").toDouble());
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_Item == bar)
        {
            CHECK(node.m_Name.contains(line));
        }
    }
}

TEST_CASE("last_bar.line: a tap opens Review, once", "[ui][last_bar]")
{
    UiRig rig;
    QQuickItem* bar = load_bar(rig, QStringLiteral("10:20  T| Mute Delay"), 7);
    QSignalSpy tapped(bar, SIGNAL(tapped()));

    rig.tap(bar);
    CHECK(tapped.count() == 1);

    // A press that slides off does not open it.
    const int finger = rig.press(bar);
    rig.move(finger, bar, QPointF(400, 100));
    rig.move(finger, bar, QPointF(500, 300));
    rig.release(finger);
    CHECK(tapped.count() == 1);

    // VoiceOver can reach it.
    REQUIRE(accessible_press(bar));
    CHECK(tapped.count() == 2);
}

TEST_CASE("last_bar.skeleton: the shape only, with no text, role or touch", "[ui][last_bar]")
{
    UiRig rig;
    QQuickItem* bar = load_bar(rig, QStringLiteral("10:20  T| Mute Delay"), 7, true);
    CHECK(rig.geometry(bar).height() == 44);
    CHECK_FALSE(require_item(rig, QStringLiteral("lastBarLine"))->isVisible());
    CHECK_FALSE(require_item(rig, QStringLiteral("lastBarCaption"))->isVisible());
    CHECK_FALSE(require_item(rig, QStringLiteral("lastBarCount"))->isVisible());
    CHECK(require_item(rig, QStringLiteral("lastBarBackground"))->isVisible());

    QSignalSpy tapped(bar, SIGNAL(tapped()));
    rig.tap(bar);
    (void)accessible_press(bar);
    CHECK(tapped.count() == 0);
    for (const Node& node : rig.accessible_tree())
    {
        CHECK_FALSE((node.m_Item == bar && node.m_InAccessibleTree));
    }
}

TEST_CASE("last_bar.a11y: names, 44 pt target and the text scales, in light and dark", "[ui][last_bar][a11y]")
{
    UiRig rig;
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        for (const int count : {0, 1, 7, 128})
        {
            const QString line = count == 0 ? QString() : QStringLiteral("10:20  T| Mute Delay");
            QQuickItem* bar = load_bar(rig, line, count);
            INFO((dark ? "dark" : "light") << ", " << count << " entries");
            expect_clean(check_everything(rig));

            // The bar grows with the text and stays 44 pt at least.
            rig.set_text_scale(3.1);
            bar->setHeight(bar->implicitHeight());
            rig.layout();
            CHECK(bar->height() >= 44);
            rig.set_text_scale(1.0);
        }
    }
}
