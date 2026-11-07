// SPDX-License-Identifier: GPL-3.0-or-later
// Sheet (docs/plan/stage-4.md, 4.4): sheet.present_dismiss, sheet.keyboard, sheet.focus, the safe area, Reduce Motion
// and the cross-cutting checks.
#include "container_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAccessible>
#include <QByteArray>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <chrono>

using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

// A page with a control that can hold focus and one that counts taps, and a sheet over it in a SheetHost with a title
// bar and a field in it, as the Other picker and the edit sheet are built.
constexpr auto HOST = R"(
    import QtQuick
    import ErrorJournalingApp
    SheetHost {
        id: host
        Rectangle {
            objectName: "opener"
            x: 20; y: 100; width: 120; height: 54
            color: Theme.colour.primary
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Log another error"
            Accessible.focusable: true
            TapHandler { onTapped: sheet.open() }
        }
        Rectangle {
            objectName: "below"
            property int taps: 0
            x: 200; y: 600; width: 120; height: 54
            color: Theme.colour.primary
            Accessible.role: Accessible.Button
            Accessible.name: "Below the sheet"
            TapHandler { onTapped: parent.taps += 1 }
        }
        sheets: [
            Sheet {
                id: sheet
                objectName: "sheet"
                accessibleName: "Log another error"
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
                NavBar {
                    objectName: "nav"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    title: "Log another error"
                    leadingText: "Cancel"
                    onLeadingTriggered: sheet.close()
                }
                Rectangle {
                    objectName: "inside"
                    property int taps: 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 120
                    width: 120
                    height: 54
                    color: Theme.colour.primary
                    Accessible.role: Accessible.Button
                    Accessible.name: "Add"
                    TapHandler { onTapped: parent.taps += 1 }
                }
                TextInput {
                    objectName: "field"
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: Theme.space.screenMargin
                    height: Theme.size.fieldHeight
                    activeFocusOnTab: true
                    font: Theme.font.body.font
                    color: Theme.colour.textPrimary
                    Accessible.role: Accessible.EditableText
                    Accessible.name: "Search error types"
                }
            }
        ]
    })";

struct Fixture
{
    UiRig m_Rig;
    QQuickItem* m_Host = nullptr;
    QQuickItem* m_Sheet = nullptr;

    Fixture()
    {
        m_Host = m_Rig.load_source(QByteArray(HOST));
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Host != nullptr);
        m_Sheet = m_Rig.find(QStringLiteral("sheet"));
        REQUIRE(m_Sheet != nullptr);
    }

    void open(std::chrono::milliseconds wait = 400ms)
    {
        QMetaObject::invokeMethod(m_Sheet, "open");
        m_Rig.advance(wait);
    }

    void close(std::chrono::milliseconds wait = 400ms)
    {
        QMetaObject::invokeMethod(m_Sheet, "close");
        m_Rig.advance(wait);
    }

    bool is_open() const
    {
        return m_Sheet->property("isOpen").toBool();
    }

    QQuickItem* panel() const
    {
        return m_Rig.find(QStringLiteral("sheetPanel"));
    }

    QRectF panel_rect() const
    {
        return m_Rig.geometry(panel());
    }

    bool focus_in_panel()
    {
        for (QQuickItem* item = m_Rig.window().activeFocusItem(); item != nullptr; item = item->parentItem())
        {
            if (item == panel())
            {
                return true;
            }
        }
        return false;
    }
};

} // namespace

TEST_CASE("sheet.present_dismiss: a closed sheet is not there, an open one slides up and a second open does nothing",
          "[ui][sheet]")
{
    Fixture f;
    QSignalSpy opened(f.m_Sheet, SIGNAL(opened()));
    QSignalSpy closed(f.m_Sheet, SIGNAL(closed()));

    CHECK_FALSE(f.is_open());
    CHECK_FALSE(f.m_Sheet->isVisible());

    QMetaObject::invokeMethod(f.m_Sheet, "open");
    CHECK(f.is_open());
    CHECK(f.m_Sheet->isVisible());
    f.m_Rig.advance(100ms);
    CHECK(opened.count() == 0); // still presenting
    f.m_Rig.advance(400ms);
    CHECK(opened.count() == 1);
    CHECK(f.is_open());

    SECTION("a large sheet is the window below the top inset, with the grabber below the inset")
    {
        const QRectF panel = f.panel_rect();
        CHECK(panel.top() == 59);
        CHECK(panel.width() == 402);
        // The sheet's bottom corners are drawn under the window's bottom edge.
        CHECK(panel.bottom() >= 874);
        CHECK(f.m_Rig.geometry(f.m_Rig.find(QStringLiteral("sheetGrabber"))).top() >= 59);
        CHECK(f.m_Rig.geometry(f.m_Rig.find(QStringLiteral("sheetGrabber"))).height() >= 44);
    }

    SECTION("a medium sheet is half the window")
    {
        f.close();
        f.m_Sheet->setProperty("detent", 0);
        f.open();
        const QRectF panel = f.panel_rect();
        CHECK(panel.top() == 437);
        CHECK(panel.top() + f.m_Sheet->property("panelHeight").toDouble() == 874);
    }

    SECTION("opening an open sheet does not present it again")
    {
        QMetaObject::invokeMethod(f.m_Sheet, "open");
        f.m_Rig.advance(500ms);
        CHECK(opened.count() == 1);
    }

    SECTION("close() dismisses once and closed is emitted once")
    {
        f.close();
        CHECK_FALSE(f.is_open());
        CHECK_FALSE(f.m_Sheet->isVisible());
        CHECK(closed.count() == 1);
        QMetaObject::invokeMethod(f.m_Sheet, "close"); // already closed
        f.m_Rig.advance(500ms);
        CHECK(closed.count() == 1);
        // It can be opened again, and closed again.
        f.open();
        CHECK(f.is_open());
        CHECK(f.panel_rect().top() == 59);
        f.close();
        CHECK(closed.count() == 2);
    }

    SECTION("close() twice while it is leaving still emits closed once")
    {
        QMetaObject::invokeMethod(f.m_Sheet, "close");
        f.m_Rig.advance(50ms);
        QMetaObject::invokeMethod(f.m_Sheet, "close");
        f.m_Rig.advance(500ms);
        CHECK(closed.count() == 1);
    }
}

TEST_CASE("sheet.present_dismiss: a drag of the grabber past the distance dismisses, a short drag springs back",
          "[ui][sheet]")
{
    Fixture f;
    QSignalSpy closed(f.m_Sheet, SIGNAL(closed()));
    f.open();
    QQuickItem* grabber = f.m_Rig.find(QStringLiteral("sheetGrabber"));
    REQUIRE(grabber != nullptr);
    const double rest = f.panel_rect().top();

    SECTION("a short drag returns to the rest position and the sheet stays open")
    {
        f.m_Rig.drag(f.m_Host, QPointF(200, rest + 10), QPointF(200, rest + 10 + 50));
        f.m_Rig.advance(600ms);
        CHECK(f.is_open());
        CHECK(closed.count() == 0);
        CHECK(f.panel_rect().top() == rest);
        CHECK(f.m_Sheet->property("dragOffset").toDouble() == 0);
    }

    SECTION("a sheet follows the finger while it is down")
    {
        // The finger moves in window coordinates; the grabber moves with the sheet.
        const int finger = f.m_Rig.press(grabber, QPointF(200, 10));
        for (int step = 1; step <= 14; ++step)
        {
            f.m_Rig.move(finger, f.m_Host, QPointF(200, rest + 10 + step * 5.0));
        }
        CHECK(f.m_Sheet->property("dragOffset").toDouble() > 0);
        CHECK(f.panel_rect().top() > rest);
        CHECK(f.panel_rect().top() == rest + f.m_Sheet->property("dragOffset").toDouble());
        // The scrim fades as the sheet goes.
        CHECK(f.m_Rig.find(QStringLiteral("sheetScrim"))->opacity() < f.m_Sheet->property("scrimLevel").toDouble());
        f.m_Rig.release(finger);
        f.m_Rig.advance(600ms);
        CHECK(f.is_open()); // let go short of the distance
        CHECK(f.panel_rect().top() == rest);
    }

    SECTION("a drag upwards does not lift the sheet above its rest position")
    {
        const int finger = f.m_Rig.press(grabber, QPointF(200, 30));
        for (int step = 1; step <= 18; ++step)
        {
            f.m_Rig.move(finger, f.m_Host, QPointF(200, rest + 30 - step * 3.0));
        }
        CHECK(f.panel_rect().top() == rest);
        f.m_Rig.release(finger);
        f.m_Rig.advance(300ms);
        CHECK(f.is_open());
    }

    SECTION("a long drag dismisses and closed is emitted once")
    {
        f.m_Rig.drag(f.m_Host, QPointF(200, rest + 10), QPointF(200, rest + 10 + 260));
        f.m_Rig.advance(600ms);
        CHECK_FALSE(f.is_open());
        CHECK(closed.count() == 1);
        CHECK(f.m_Sheet->property("dragOffset").toDouble() == 0);
    }

    SECTION("a sheet that is not dismissible ignores the drag")
    {
        f.m_Sheet->setProperty("dismissible", false);
        f.m_Rig.drag(f.m_Host, QPointF(200, rest + 10), QPointF(200, rest + 10 + 260));
        f.m_Rig.advance(600ms);
        CHECK(f.is_open());
        CHECK(closed.count() == 0);
    }
}

TEST_CASE("sheet.present_dismiss: a tap on the scrim dismisses, a tap on the sheet does not", "[ui][sheet]")
{
    Fixture f;
    QSignalSpy closed(f.m_Sheet, SIGNAL(closed()));
    f.open();
    QQuickItem* scrim = f.m_Rig.find(QStringLiteral("sheetScrim"));
    REQUIRE(scrim != nullptr);

    SECTION("on the sheet, away from its controls")
    {
        f.m_Rig.tap(f.panel(), QPointF(200, 300));
        f.m_Rig.advance(600ms);
        CHECK(f.is_open());
        CHECK(closed.count() == 0);
    }

    SECTION("on the scrim, over the top inset")
    {
        f.m_Rig.tap(scrim, QPointF(200, 20));
        f.m_Rig.advance(600ms);
        CHECK_FALSE(f.is_open());
        CHECK(closed.count() == 1);
    }

    SECTION("on a control inside the sheet: the control takes it and the sheet stays")
    {
        QQuickItem* inside = f.m_Rig.find(QStringLiteral("inside"));
        REQUIRE(inside != nullptr);
        f.m_Rig.tap(inside);
        f.m_Rig.advance(600ms);
        CHECK(inside->property("taps").toInt() == 1);
        CHECK(f.is_open());
        CHECK(closed.count() == 0);
    }

    SECTION("not when the sheet is not dismissible")
    {
        f.m_Sheet->setProperty("dismissible", false);
        f.m_Rig.tap(scrim, QPointF(200, 20));
        f.m_Rig.advance(600ms);
        CHECK(f.is_open());
        CHECK(closed.count() == 0);
    }

    SECTION("a tap on the Cancel button of its title bar dismisses")
    {
        f.m_Rig.tap(f.m_Rig.find(QStringLiteral("navLeading")));
        f.m_Rig.advance(600ms);
        CHECK_FALSE(f.is_open());
        CHECK(closed.count() == 1);
    }

    SECTION("the scrim is a barrier and not a control: a screen reader reaches Close on the grabber instead")
    {
        for (const Node& node : f.m_Rig.tree())
        {
            if (node.m_Item == scrim)
            {
                CHECK_FALSE(node.m_Interactive);
                CHECK_FALSE(node.m_InAccessibleTree);
            }
        }
    }
}

TEST_CASE("sheet.present_dismiss: touches do not reach what is under the sheet or its scrim", "[ui][sheet]")
{
    Fixture f;
    QQuickItem* below = f.m_Rig.find(QStringLiteral("below"));
    REQUIRE(below != nullptr);

    f.m_Rig.tap(below);
    CHECK(below->property("taps").toInt() == 1); // closed: the page works

    f.open();
    // The control is under the sheet now.
    f.m_Rig.tap(below);
    CHECK(below->property("taps").toInt() == 1);
    CHECK(f.is_open());
}

TEST_CASE("sheet.keyboard: the keyboard lifts the sheet's bottom edge, and the content ends above it", "[ui][sheet]")
{
    Fixture f;
    constexpr double KEYBOARD = 336;
    QQuickItem* field = f.m_Rig.find(QStringLiteral("field"));
    REQUIRE(field != nullptr);

    SECTION("a medium sheet is lifted whole")
    {
        f.m_Sheet->setProperty("detent", 0);
        f.open();
        const double before = f.panel_rect().top();
        // Without the keyboard the field sits above the home indicator.
        CHECK(f.m_Rig.geometry(field).bottom() <= 874 - 34);

        f.m_Sheet->setProperty("keyboardHeight", KEYBOARD);
        f.m_Rig.layout();
        const double height = f.m_Sheet->property("panelHeight").toDouble();
        CHECK(height == 437); // as tall as before
        CHECK(f.panel_rect().top() + height == 874 - KEYBOARD);
        CHECK(f.panel_rect().top() < before);
        CHECK(f.m_Rig.geometry(field).bottom() <= 874 - KEYBOARD);
        CHECK(f.m_Rig.geometry(field).top() >= 59);

        f.m_Sheet->setProperty("keyboardHeight", 0);
        f.m_Rig.layout();
        CHECK(f.panel_rect().top() == before);
    }

    SECTION("a large sheet gets shorter and stays below the top inset")
    {
        f.open();
        f.m_Sheet->setProperty("keyboardHeight", KEYBOARD);
        f.m_Rig.layout();
        CHECK(f.panel_rect().top() == 59);
        CHECK(f.m_Sheet->property("panelHeight").toDouble() == 874 - KEYBOARD - 59);
        CHECK(f.m_Rig.geometry(field).bottom() <= 874 - KEYBOARD);
        expect_clean(check_names(f.m_Rig));
    }

    SECTION("the field is not covered at any text size")
    {
        f.open();
        f.m_Sheet->setProperty("keyboardHeight", KEYBOARD);
        for (const double scale : {1.0, 1.3, 3.1})
        {
            f.m_Rig.set_text_scale(scale);
            CHECK(f.m_Rig.geometry(field).bottom() <= 874 - KEYBOARD);
            CHECK(f.m_Rig.geometry(field).top() >= f.m_Rig.geometry(f.panel()).top());
        }
    }
}

TEST_CASE("sheet.focus: focus enters the sheet, stays in it and returns to the opener", "[ui][sheet]")
{
    Fixture f;
    QQuickItem* opener = f.m_Rig.find(QStringLiteral("opener"));
    REQUIRE(opener != nullptr);
    opener->forceActiveFocus();
    REQUIRE(f.m_Rig.window().activeFocusItem() == opener);

    f.open();
    CHECK(f.focus_in_panel());

    SECTION("Tab and Backtab wrap inside the sheet")
    {
        // More presses than the sheet has focusable items, in both directions.
        for (int i = 0; i < 8; ++i)
        {
            f.m_Rig.key(Qt::Key_Tab);
            CHECK(f.focus_in_panel());
            CHECK(f.m_Rig.window().activeFocusItem() != opener);
        }
        for (int i = 0; i < 8; ++i)
        {
            f.m_Rig.key(Qt::Key_Backtab);
            CHECK(f.focus_in_panel());
            CHECK(f.m_Rig.window().activeFocusItem() != opener);
        }
    }

    SECTION("Tab reaches the field")
    {
        bool reached = false;
        for (int i = 0; i < 4 && !reached; ++i)
        {
            f.m_Rig.key(Qt::Key_Tab);
            reached = f.m_Rig.window().activeFocusItem() == f.m_Rig.find(QStringLiteral("field"));
        }
        CHECK(reached);
    }

    SECTION("Escape dismisses a dismissible sheet and focus goes back to where it was")
    {
        f.m_Rig.key(Qt::Key_Escape);
        f.m_Rig.advance(600ms);
        CHECK_FALSE(f.is_open());
        CHECK(f.m_Rig.window().activeFocusItem() == opener);
    }

    SECTION("Escape does nothing to a sheet that is not dismissible")
    {
        f.m_Sheet->setProperty("dismissible", false);
        f.m_Rig.key(Qt::Key_Escape);
        f.m_Rig.advance(600ms);
        CHECK(f.is_open());
        CHECK(f.focus_in_panel());
    }

    SECTION("closing by the title bar returns focus too")
    {
        f.m_Rig.tap(f.m_Rig.find(QStringLiteral("navLeading")));
        f.m_Rig.advance(600ms);
        CHECK(f.m_Rig.window().activeFocusItem() == opener);
    }
}

TEST_CASE("sheet.focus: the sheet is a named dialog that a screen reader can land on", "[ui][sheet]")
{
    Fixture f;
    f.open();
    bool found = false;
    for (const Node& node : f.m_Rig.accessible_tree())
    {
        if (node.m_Item == f.panel())
        {
            found = true;
            CHECK(node.m_Name == QStringLiteral("Log another error"));
            CHECK(node.m_Role == QAccessible::Dialog);
        }
    }
    CHECK(found);

    // Close is reachable without a gesture: the grabber is a named button.
    bool grabber = false;
    for (const Node& node : f.m_Rig.accessible_tree())
    {
        if (node.m_ObjectName == QStringLiteral("sheetGrabber"))
        {
            grabber = true;
            CHECK(node.m_Role == QAccessible::Button);
            CHECK(node.m_Name == QStringLiteral("Close"));
        }
    }
    CHECK(grabber);
}

TEST_CASE("sheet: an open sheet passes the cross-cutting checks, in light and dark, large and medium", "[ui][sheet]")
{
    Fixture f;
    const bool dark = GENERATE(false, true);
    const int detent = GENERATE(0, 1);
    INFO("dark " << dark << ", detent " << detent);
    f.m_Rig.set_dark(dark);
    f.m_Sheet->setProperty("detent", detent);
    // The page under the sheet is disabled while it is open (SheetHost), but it is still in the tree, so it is hidden
    // here: the reading order of the sheet is what is being checked.
    HideBase hide_opener(f.m_Rig.find(QStringLiteral("opener")));
    HideBase hide_below(f.m_Rig.find(QStringLiteral("below")));
    f.open();
    expect_clean(check_everything(f.m_Rig));
    expect_clean(check_safe_area(f.m_Rig, 59, 34));
}

TEST_CASE("sheet.safe_area: nothing interactive enters an inset, at 59 and 34 and at 79 and 54", "[ui][sheet]")
{
    Fixture f;
    f.m_Sheet->setProperty("overrideTop", 79);
    f.m_Sheet->setProperty("overrideBottom", 54);
    f.open();
    CHECK(f.panel_rect().top() == 79);
    expect_clean(check_safe_area(f.m_Rig, 79, 54));
    CHECK(f.m_Rig.geometry(f.m_Rig.find(QStringLiteral("field"))).bottom() <= 874 - 54);
}

TEST_CASE("motion.reduce: with Reduce Motion the sheet fades in 100 ms and does not slide", "[ui][sheet][motion]")
{
    Fixture f;

    SECTION("the check finds the 320 ms slide when Reduce Motion is off")
    {
        CHECK_FALSE(check_motion_reduced(f.m_Rig).empty());
    }

    SECTION("with it on, no animation is longer than 100 ms and none slides")
    {
        f.m_Rig.set_reduce_motion(true);
        expect_clean(check_motion_reduced(f.m_Rig));
        f.open(250ms);
        expect_clean(check_motion_reduced(f.m_Rig));
        CHECK(f.is_open());
        CHECK(f.panel_rect().top() == 59);
        f.close(250ms);
        CHECK_FALSE(f.is_open());
    }
}
