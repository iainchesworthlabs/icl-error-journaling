// SPDX-License-Identifier: GPL-3.0-or-later
// HelpSheet (Stage 10, 10.6): a topic of HelpTopics as a heading, paragraphs and "Read more in the guide". It is tested
// on its own here; the shell tests (app_flow_test.cpp, `help.*`) open it from the screens.
#include "container_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStringList>

using namespace ej::ui;
using namespace ej::ui::containers;
using namespace std::chrono_literals;

namespace
{

QQuickItem* load_sheet(UiRig& rig)
{
    QQuickItem* sheet = rig.load(QStringLiteral("HelpSheet"), {{QStringLiteral("overrideInsets"), true},
                                                               {QStringLiteral("overrideTop"), 59},
                                                               {QStringLiteral("overrideBottom"), 34}});
    INFO(rig.errors().toStdString());
    REQUIRE(sheet != nullptr);
    return sheet;
}

void show(UiRig& rig, QQuickItem* sheet, const QString& topic)
{
    REQUIRE(QMetaObject::invokeMethod(sheet, "show", Q_ARG(QString, topic)));
    rig.advance(500ms);
}

QStringList paragraphs_of(UiRig& rig)
{
    QStringList texts;
    QQuickItem* column = rig.find(QStringLiteral("helpParagraphs"));
    REQUIRE(column != nullptr);
    for (QQuickItem* child : column->childItems())
    {
        const QString text = child->property("text").toString();
        if (!text.isEmpty())
        {
            texts.append(text);
        }
    }
    return texts;
}

} // namespace

TEST_CASE("help.sheet_component: a topic opens with its heading and its paragraphs in order, and a screen reader names "
          "the sheet for it",
          "[ui][help][help_sheet]")
{
    UiRig rig;
    QQuickItem* sheet = load_sheet(rig);
    CHECK_FALSE(sheet->property("isOpen").toBool());

    show(rig, sheet, QStringLiteral("errorTypes"));
    CHECK(sheet->property("isOpen").toBool());
    CHECK(sheet->property("heading").toString() == QStringLiteral("Error types"));
    CHECK(sheet->property("accessibleName").toString() == QStringLiteral("Error types"));
    const QStringList texts = paragraphs_of(rig);
    REQUIRE(texts.size() == 4);
    CHECK(texts.at(0).contains(QStringLiteral("whole list")));
    CHECK(texts.at(3).contains(QStringLiteral("retire")));

    // The paragraphs are in reading order: each starts under the one before.
    double previous_bottom = -1;
    for (QQuickItem* child : rig.find(QStringLiteral("helpParagraphs"))->childItems())
    {
        if (child->property("text").toString().isEmpty())
        {
            continue;
        }
        CHECK(child->y() >= previous_bottom);
        previous_bottom = child->y() + child->height();
    }

    // The heading is the title of the bar, which a screen reader reads as a heading.
    const std::vector<Node> reached = rig.accessible_tree();
    const Node* bar_title = find_node(reached, QStringLiteral("navTitle"));
    REQUIRE(bar_title != nullptr);
    CHECK(bar_title->m_Name == QStringLiteral("Error types"));
}

TEST_CASE(
    "help.sheet_component: \"Read more in the guide\" asks for the address of the topic's section once, Done closes, "
    "and an unknown topic does not open",
    "[ui][help][help_sheet]")
{
    UiRig rig;
    QQuickItem* sheet = load_sheet(rig);
    QSignalSpy requested(sheet, SIGNAL(guideRequested(QString)));

    show(rig, sheet, QStringLiteral("templates"));
    rig.tap(rig.find(QStringLiteral("helpReadMore")));
    REQUIRE(requested.count() == 1);
    const QString address = requested.takeFirst().at(0).toString();
    CHECK(address.startsWith(QStringLiteral("https://")));
    CHECK(address.endsWith(QStringLiteral("/guide/setup/#note-templates")));
    // The sheet stays open: the browser opens over the app, and the person comes back to the help.
    CHECK(sheet->property("isOpen").toBool());

    // The words under the button say that the help works without a connection.
    CHECK(rig.find(QStringLiteral("helpOfflineNote"))->property("text").toString() ==
          QStringLiteral("The guide opens in your browser. This help works without a connection."));

    rig.tap(rig.find(QStringLiteral("navTrailing"), rig.find(QStringLiteral("helpBar"))));
    rig.advance(500ms);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(requested.count() == 0);

    show(rig, sheet, QStringLiteral("noSuchTopic"));
    CHECK_FALSE(sheet->property("isOpen").toBool());
}

TEST_CASE(
    "help.sheet_component: the sheet is as tall as its text, and at the largest text sizes the text scrolls with the "
    "button still on screen",
    "[ui][help][help_sheet]")
{
    UiRig rig;
    QQuickItem* sheet = load_sheet(rig);
    show(rig, sheet, QStringLiteral("board"));
    const QRectF window = rig.geometry(sheet);
    QQuickItem* panel = rig.find(QStringLiteral("sheetPanel"));
    REQUIRE(panel != nullptr);

    SECTION("the usual size: the sheet is shorter than the window and the text does not scroll")
    {
        const QRectF panel_rect = rig.geometry(panel);
        CHECK(panel_rect.height() < window.height() - 100);
        QQuickItem* flick = rig.find(QStringLiteral("helpFlickable"));
        CHECK(flick->property("contentHeight").toDouble() <= flick->height() + 1.0);
    }

    SECTION("the largest size: the sheet is inside the window, the text scrolls and the button is whole")
    {
        rig.set_text_scale(3.0);
        rig.advance(500ms);
        rig.layout();
        const QRectF panel_rect = rig.geometry(panel);
        CHECK(panel_rect.top() >= 0);
        // A bottom sheet draws its rounded bottom corners below the window.
        CHECK(panel_rect.bottom() <= window.bottom() + panel->property("radius").toDouble() + 1.0);
        QQuickItem* flick = rig.find(QStringLiteral("helpFlickable"));
        CHECK(flick->property("contentHeight").toDouble() > flick->height());
        const QRectF button = rig.geometry(rig.find(QStringLiteral("helpReadMore")));
        CHECK(button.bottom() <= window.bottom() + 1.0);
        CHECK(button.top() >= rig.geometry(flick).bottom() - 1.0);
        CHECK(button.height() >= 44.0);
        for (QQuickItem* child : rig.find(QStringLiteral("helpParagraphs"))->childItems())
        {
            CHECK(child->width() <= panel_rect.width());
        }
    }
}
