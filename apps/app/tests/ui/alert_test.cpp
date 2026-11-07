// SPDX-License-Identifier: GPL-3.0-or-later
// Alert (docs/plan/stage-4.md, 4.4): alert.modal (blocks input below it, default and destructive roles named), focus,
// contrast, Reduce Motion and the cross-cutting checks.
#include "container_support.hpp"
#include "rig/color_math.hpp"
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
using namespace ej::ui::containers;
using namespace std::chrono_literals;

namespace
{

// The destructive confirmation of the History screen over a page with two controls.
constexpr auto DELETE_LOG = R"(
    import QtQuick
    import ErrorJournalingApp
    SheetHost {
        Rectangle {
            objectName: "opener"
            x: 20; y: 100; width: 120; height: 54
            color: Theme.colour.primary
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: "Delete"
            Accessible.focusable: true
            TapHandler { onTapped: alert.open() }
        }
        Rectangle {
            objectName: "below"
            property int taps: 0
            x: 200; y: 600; width: 120; height: 54
            color: Theme.colour.primary
            Accessible.role: Accessible.Button
            Accessible.name: "Below the alert"
            TapHandler { onTapped: parent.taps += 1 }
        }
        sheets: [
            Alert {
                id: alert
                objectName: "alert"
                title: "Delete this log?"
                message: "Thu 5 Nov 2026, UNIT-27 at North Site, 12 entries. It is removed from this phone and can't be brought back."
                icon: Alert.Trash
                destructive: true
                confirmText: "Delete log"
                cancelText: "Cancel"
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
            }
        ]
    })";

// The new-day prompt: a default action, an alternate and a cancel.
constexpr auto NEW_DAY = R"(
    import QtQuick
    import ErrorJournalingApp
    SheetHost {
        sheets: [
            Alert {
                id: alert
                objectName: "alert"
                title: "It's Sunday 8 November"
                message: "Saturday's log has 7 entries and hasn't been shared yet. Starting a new log keeps it in History."
                icon: Alert.Calendar
                confirmText: "Start a new log"
                alternateText: "Share Saturday's log first"
                cancelText: "Keep Saturday's log open"
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
            }
        ]
    })";

struct Fixture
{
    UiRig m_Rig;
    QQuickItem* m_Host = nullptr;
    QQuickItem* m_Alert = nullptr;

    explicit Fixture(const char* source = DELETE_LOG)
    {
        m_Host = m_Rig.load_source(QByteArray(source));
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Host != nullptr);
        m_Alert = m_Rig.find(QStringLiteral("alert"));
        REQUIRE(m_Alert != nullptr);
    }

    void open()
    {
        QMetaObject::invokeMethod(m_Alert, "open");
        run(m_Rig, 500ms);
    }

    bool is_open() const
    {
        return m_Alert->property("isOpen").toBool();
    }

    QQuickItem* panel() const
    {
        return m_Rig.find(QStringLiteral("sheetPanel"));
    }

    QQuickItem* button(const char* name) const
    {
        return m_Rig.find(QString::fromLatin1(name));
    }
};

} // namespace

TEST_CASE("alert.modal: it blocks everything below it and is not dismissed from outside", "[ui][alert]")
{
    Fixture f;
    QQuickItem* below = f.m_Rig.find(QStringLiteral("below"));
    QSignalSpy closed(f.m_Alert, SIGNAL(closed()));
    QSignalSpy cancelled(f.m_Alert, SIGNAL(cancelled()));
    f.m_Rig.tap(below);
    CHECK(below->property("taps").toInt() == 1);

    f.open();
    REQUIRE(f.is_open());

    SECTION("a tap on what is under it does nothing")
    {
        f.m_Rig.tap(below);
        CHECK(below->property("taps").toInt() == 1);
        f.m_Rig.tap(f.m_Rig.find(QStringLiteral("opener")));
        CHECK(f.is_open());
    }

    SECTION("a tap on the scrim, a drag of the card and Escape leave it open")
    {
        f.m_Rig.tap(f.m_Rig.find(QStringLiteral("sheetScrim")), QPointF(10, 10));
        run(f.m_Rig, 500ms);
        CHECK(f.is_open());
        const QRectF card = f.m_Rig.geometry(f.panel());
        f.m_Rig.drag(f.m_Host, card.center(), card.center() + QPointF(0, 300));
        run(f.m_Rig, 500ms);
        CHECK(f.is_open());
        CHECK(f.m_Rig.geometry(f.panel()) == card);
        f.m_Rig.key(Qt::Key_Escape);
        run(f.m_Rig, 500ms);
        CHECK(f.is_open());
        CHECK(closed.count() == 0);
        CHECK(cancelled.count() == 0);
    }

    SECTION("a tap on the card away from its buttons does nothing")
    {
        f.m_Rig.tap(f.m_Rig.find(QStringLiteral("alertTitle")));
        run(f.m_Rig, 500ms);
        CHECK(f.is_open());
    }
}

TEST_CASE("alert.modal: each button closes the alert and then emits its own signal, once", "[ui][alert]")
{
    Fixture f(NEW_DAY);
    QSignalSpy confirmed(f.m_Alert, SIGNAL(confirmed()));
    QSignalSpy alternated(f.m_Alert, SIGNAL(alternated()));
    QSignalSpy cancelled(f.m_Alert, SIGNAL(cancelled()));
    QSignalSpy closed(f.m_Alert, SIGNAL(closed()));

    const char* button = GENERATE("alertConfirm", "alertAlternate", "alertCancel");
    INFO(button);
    f.open();
    f.m_Rig.tap(f.button(button));
    CHECK(confirmed.count() == (std::string(button) == "alertConfirm" ? 1 : 0));
    CHECK(alternated.count() == (std::string(button) == "alertAlternate" ? 1 : 0));
    CHECK(cancelled.count() == (std::string(button) == "alertCancel" ? 1 : 0));
    run(f.m_Rig, 600ms);
    CHECK_FALSE(f.is_open());
    CHECK(closed.count() == 1);
}

TEST_CASE("alert.modal: the default and the destructive buttons are named, and a button not asked for is not there",
          "[ui][alert]")
{
    SECTION("a destructive confirmation: Delete log is named and is not the default button, Cancel is")
    {
        Fixture f;
        f.open();
        const std::vector<Node> nodes = f.m_Rig.accessible_tree();
        const Node* confirm = find_node(nodes, QStringLiteral("alertConfirm"));
        const Node* cancel = find_node(nodes, QStringLiteral("alertCancel"));
        REQUIRE(confirm != nullptr);
        REQUIRE(cancel != nullptr);
        CHECK(confirm->m_Role == QAccessible::Button);
        CHECK(confirm->m_Name == QStringLiteral("Delete log"));
        CHECK(cancel->m_Name == QStringLiteral("Cancel"));
        CHECK_FALSE(QAccessible::queryAccessibleInterface(confirm->m_Item)->state().defaultButton);
        // There was no alternate text.
        CHECK(find_shown(nodes, QStringLiteral("alertAlternate")) == nullptr);
    }

    SECTION("the new-day prompt: the first action is the default button")
    {
        Fixture f(NEW_DAY);
        f.open();
        const std::vector<Node> nodes = f.m_Rig.accessible_tree();
        const Node* confirm = find_node(nodes, QStringLiteral("alertConfirm"));
        REQUIRE(confirm != nullptr);
        CHECK(confirm->m_Name == QStringLiteral("Start a new log"));
        CHECK(QAccessible::queryAccessibleInterface(confirm->m_Item)->state().defaultButton);
        CHECK(find_node(nodes, QStringLiteral("alertAlternate"))->m_Name ==
              QStringLiteral("Share Saturday's log first"));
        CHECK(find_node(nodes, QStringLiteral("alertCancel"))->m_Name == QStringLiteral("Keep Saturday's log open"));
    }

    SECTION("the alert is a named dialog described by its message, and its words are not read twice")
    {
        Fixture f;
        f.open();
        const std::vector<Node> nodes = f.m_Rig.accessible_tree();
        const Node* card = find_node(nodes, QStringLiteral("sheetPanel"));
        REQUIRE(card != nullptr);
        CHECK(card->m_Role == QAccessible::Dialog);
        CHECK(card->m_Name == QStringLiteral("Delete this log?"));
        CHECK(card->m_Description.startsWith(QStringLiteral("Thu 5 Nov 2026")));
        CHECK(find_node(nodes, QStringLiteral("alertTitle")) == nullptr);
        CHECK(find_node(nodes, QStringLiteral("alertMessage")) == nullptr);
    }

    SECTION("a screen reader's activation of a button does what a tap does")
    {
        Fixture f;
        QSignalSpy confirmed(f.m_Alert, SIGNAL(confirmed()));
        f.open();
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(f.button("alertConfirm"));
        REQUIRE(iface != nullptr);
        iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        CHECK(confirmed.count() == 1);
    }
}

TEST_CASE("alert.modal: the card follows the design", "[ui][alert]")
{
    Fixture f;
    f.open();
    QQuickItem* panel = f.panel();
    const QRectF card = f.m_Rig.geometry(panel);

    CHECK(panel->property("radius").toDouble() == 22);
    // 32 pt from the window's sides.
    CHECK(card.left() == 32);
    CHECK(card.right() == 402 - 32);
    // Centred between the insets.
    CHECK(card.center().y() == (59.0 + (874 - 34)) / 2);
    // The icon is a 48 pt circle at the top, centred.
    const QRectF icon = f.m_Rig.geometry(f.m_Rig.find(QStringLiteral("alertIcon")));
    CHECK(icon.width() == 48);
    CHECK(icon.height() == 48);
    CHECK(icon.center().x() == 201);
    CHECK(icon.top() > card.top());
    // The buttons are 54 pt and 48 pt, and full width of the card's content.
    CHECK(f.m_Rig.geometry(f.button("alertConfirm")).height() == 54);
    CHECK(f.m_Rig.geometry(f.button("alertCancel")).height() == 48);
    CHECK(f.m_Rig.geometry(f.button("alertConfirm")).width() == card.width() - 40);
    CHECK(f.m_Rig.geometry(f.button("alertConfirm")).bottom() < f.m_Rig.geometry(f.button("alertCancel")).top());
    // Everything is inside the card.
    for (const char* name : {"alertIcon", "alertTitle", "alertMessage", "alertConfirm", "alertCancel"})
    {
        const QRectF part = f.m_Rig.geometry(f.button(name));
        CHECK(part.top() >= card.top());
        CHECK(part.bottom() <= card.bottom());
    }
}

TEST_CASE("alert.modal: a card taller than the window scrolls instead of leaving it", "[ui][alert]")
{
    Fixture f(NEW_DAY);
    f.open();
    f.m_Rig.set_text_scale(3.1);
    const QRectF card = f.m_Rig.geometry(f.panel());
    CHECK(card.top() >= 59);
    CHECK(card.bottom() <= 874 - 34);
    // The content is taller than the card, so the card scrolls; the buttons are not clipped away.
    QQuickItem* confirm = f.button("alertConfirm");
    REQUIRE(confirm != nullptr);
    CHECK(f.m_Rig.geometry(confirm).height() >= 54);
    expect_clean(check_text_scale(f.m_Rig));
}

TEST_CASE("alert.focus: focus enters the alert, stays in it and returns to the opener", "[ui][alert]")
{
    Fixture f;
    QQuickItem* opener = f.m_Rig.find(QStringLiteral("opener"));
    opener->forceActiveFocus();
    REQUIRE(f.m_Rig.window().activeFocusItem() == opener);
    f.open();

    const auto in_alert = [&f]
    {
        for (QQuickItem* item = f.m_Rig.window().activeFocusItem(); item != nullptr; item = item->parentItem())
        {
            if (item == f.panel())
            {
                return true;
            }
        }
        return false;
    };
    CHECK(in_alert());
    for (int i = 0; i < 6; ++i)
    {
        f.m_Rig.key(Qt::Key_Tab);
        CHECK(in_alert());
    }
    for (int i = 0; i < 6; ++i)
    {
        f.m_Rig.key(Qt::Key_Backtab);
        CHECK(in_alert());
    }

    SECTION("Return on the focused button does what a tap does")
    {
        QSignalSpy confirmed(f.m_Alert, SIGNAL(confirmed()));
        QSignalSpy cancelled(f.m_Alert, SIGNAL(cancelled()));
        f.button("alertConfirm")->forceActiveFocus();
        f.m_Rig.key(Qt::Key_Return);
        CHECK(confirmed.count() == 1);
        CHECK(cancelled.count() == 0);
        run(f.m_Rig, 600ms);
        CHECK(f.m_Rig.window().activeFocusItem() == opener);
    }

    SECTION("closing by Cancel returns focus")
    {
        f.m_Rig.tap(f.button("alertCancel"));
        run(f.m_Rig, 600ms);
        CHECK(f.m_Rig.window().activeFocusItem() == opener);
    }
}

TEST_CASE("alert.contrast: the labels of the buttons and the icon pass, in light and dark", "[ui][alert][contrast]")
{
    Fixture f(NEW_DAY);
    for (const bool dark : {false, true})
    {
        f.m_Rig.set_dark(dark);
        INFO("dark " << dark);
        const QColor card = theme_colour(f.m_Rig, "surfaceRaised");
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "textPrimary"), card) >= 4.5);
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "textBody"), card) >= 4.5);
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "textOnPrimary"), theme_colour(f.m_Rig, "primary")) >= 4.5);
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "textOnPrimary"), theme_colour(f.m_Rig, "primaryPressed")) >= 4.5);
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "textOnDestructive"), theme_colour(f.m_Rig, "destructiveFill")) >=
              4.5);
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "primaryText"), theme_colour(f.m_Rig, "primaryTint")) >= 4.5);
        // The cancel button is a plain label on the card.
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "primaryText"), card) >= 4.5);
        // The icons, as indicators on their circles.
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "primaryText"), theme_colour(f.m_Rig, "primaryTint")) >= 3.0);
        CHECK(contrast_ratio(theme_colour(f.m_Rig, "destructive"), theme_colour(f.m_Rig, "bannerError")) >= 3.0);
    }
}

TEST_CASE("alert: passes the cross-cutting checks in light and dark, for both prompts", "[ui][alert]")
{
    const bool delete_log = GENERATE(true, false);
    const bool dark = GENERATE(false, true);
    INFO("delete log " << delete_log << ", dark " << dark);
    Fixture f(delete_log ? DELETE_LOG : NEW_DAY);
    f.m_Rig.set_dark(dark);
    if (delete_log)
    {
        // The page under the alert is disabled but still in the tree; the reading order of the alert is what is
        // checked.
        f.m_Rig.find(QStringLiteral("opener"))->setVisible(false);
        f.m_Rig.find(QStringLiteral("below"))->setVisible(false);
    }
    f.open();
    expect_clean(check_everything(f.m_Rig));
    expect_clean(check_safe_area(f.m_Rig, 59, 34));
}

TEST_CASE("motion.reduce: with Reduce Motion the alert fades in 100 ms and does not move or scale",
          "[ui][alert][motion]")
{
    Fixture f;

    SECTION("the check finds the 320 ms presentation when Reduce Motion is off")
    {
        CHECK_FALSE(check_motion_reduced(f.m_Rig).empty());
    }

    SECTION("with it on, nothing is longer than 100 ms and nothing slides")
    {
        f.m_Rig.set_reduce_motion(true);
        expect_clean(check_motion_reduced(f.m_Rig));
        QMetaObject::invokeMethod(f.m_Alert, "open");
        run(f.m_Rig, 250ms);
        CHECK(f.is_open());
        CHECK(f.panel()->opacity() == 1);
        CHECK(f.panel()->scale() == 1);
        f.m_Rig.tap(f.button("alertCancel"));
        run(f.m_Rig, 250ms);
        CHECK_FALSE(f.is_open());
    }
}
