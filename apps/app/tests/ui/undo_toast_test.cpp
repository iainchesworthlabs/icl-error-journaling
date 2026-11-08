// SPDX-License-Identifier: GPL-3.0-or-later
// UndoToast (docs/plan/stage-4.md, 4.5): toast.text_and_bar, toast.undo, toast.repeat and toast.lifecycle, with plain
// property values for what UndoController offers, and the accessibility and motion checks.
#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QVariantMap>
#include <algorithm>
#include <chrono>

using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

constexpr int WINDOW_MS = 5000;

QVariantMap toast_properties(bool active, const QString& text, int generation, bool undoing = false)
{
    return {{QStringLiteral("active"), active},
            {QStringLiteral("text"), text},
            {QStringLiteral("undoing"), undoing},
            {QStringLiteral("generation"), generation},
            {QStringLiteral("windowMs"), WINDOW_MS}};
}

QQuickItem* load_toast(UiRig& rig, const QVariantMap& properties)
{
    QQuickItem* toast = rig.load(QStringLiteral("UndoToast"), properties);
    REQUIRE(toast != nullptr);
    // The toast is as high as its card; the rig sizes a loaded item to the window.
    toast->setWidth(370);
    toast->setHeight(toast->implicitHeight());
    rig.advance(16ms);
    return toast;
}

/// Waits for an animator of `duration_ms` to have ended and written its value back: its time, and a few frames.
void advance_through(UiRig& rig, int duration_ms)
{
    rig.advance(std::chrono::milliseconds(duration_ms + 48));
}

QObject* object_named(QQuickItem* from, const QString& name)
{
    auto* found = from->findChild<QObject*>(name);
    REQUIRE(found != nullptr);
    return found;
}

QObject* theme_group(UiRig& rig, const char* name)
{
    auto* group = rig.theme()->property(name).value<QObject*>();
    REQUIRE(group != nullptr);
    return group;
}

} // namespace

TEST_CASE("toast.text_and_window: shows the text, a new window starts on a new generation and not otherwise, and there "
          "is no countdown bar",
          "[ui][toast]")
{
    UiRig rig;
    QQuickItem* toast = load_toast(rig, toast_properties(true, QStringLiteral("Mute Delay logged at 10:20"), 1));
    rig.advance(300ms);

    CHECK(require_item(rig, QStringLiteral("toastText"))->property("text").toString() ==
          QStringLiteral("Mute Delay logged at 10:20"));
    CHECK(toast->property("windowStarts").toInt() == 1);
    // The mock-ups have no countdown bar (owner decision), so the toast draws none.
    CHECK(rig.find(QStringLiteral("toastBarTrack")) == nullptr);
    // The card is the design's 52 pt.
    CHECK(rig.geometry(require_item(rig, QStringLiteral("toastCard"))).height() == 52);

    // The same generation with another line is no new window.
    rig.advance(3000ms);
    toast->setProperty("text", QStringLiteral("Mute Delay logged at 10:21"));
    rig.advance(16ms);
    CHECK(toast->property("windowStarts").toInt() == 1);
    CHECK(require_item(rig, QStringLiteral("toastText"))->property("text").toString() ==
          QStringLiteral("Mute Delay logged at 10:21"));

    // A new generation is.
    toast->setProperty("generation", 2);
    rig.advance(16ms);
    CHECK(toast->property("windowStarts").toInt() == 2);
}

TEST_CASE("toast.undo: the button asks once, and is disabled while the undo is being committed", "[ui][toast]")
{
    UiRig rig;
    QQuickItem* toast = load_toast(rig, toast_properties(true, QStringLiteral("Mute Delay logged at 10:20"), 1));
    rig.advance(300ms);
    QSignalSpy requested(toast, SIGNAL(undoRequested()));
    QQuickItem* button = rig.find(QStringLiteral("toastUndo"));
    REQUIRE(button != nullptr);

    rig.tap(button);
    CHECK(requested.count() == 1);

    // VoiceOver presses it too.
    REQUIRE(accessible_press(button));
    CHECK(requested.count() == 2);

    // While the undo is committed, neither a touch nor an activation asks again.
    toast->setProperty("undoing", true);
    rig.advance(16ms);
    rig.tap(button);
    (void)accessible_press(button);
    CHECK(requested.count() == 2);
    CHECK(button->opacity() < 1.0);

    toast->setProperty("undoing", false);
    rig.advance(16ms);
    rig.tap(button);
    CHECK(requested.count() == 3);
    CHECK(button->opacity() == 1.0);
}

TEST_CASE("toast.undo: the button is named for what it undoes, drawn 40 pt high with a 44 pt touch area", "[ui][toast]")
{
    UiRig rig;
    QQuickItem* toast = load_toast(rig, toast_properties(true, QStringLiteral("Mute Delay logged at 10:20"), 1));
    rig.advance(300ms);
    const QRectF rect = rig.geometry(rig.find(QStringLiteral("toastUndo")));
    CHECK(rect.width() >= 44);
    CHECK(rect.height() == 40);
    // The tap handler's margin makes the touch area 44 pt.
    const QObject* tap = object_named(toast, QStringLiteral("toastUndoTap"));
    CHECK(rect.height() + 2 * tap->property("margin").toDouble() >= 44);
    expect_clean(check_targets(rig));

    bool found = false;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_ObjectName == QStringLiteral("toastUndo"))
        {
            found = true;
            CHECK(node.m_Name == QStringLiteral("Undo, Mute Delay logged at 10:20"));
            CHECK(node.m_Name.startsWith(QStringLiteral("Undo")));
        }
    }
    CHECK(found);
}

TEST_CASE("toast.repeat: a repeat changes the text in place, without the toast closing and opening", "[ui][toast]")
{
    UiRig rig;
    QQuickItem* toast = load_toast(rig, toast_properties(true, QStringLiteral("Mute Delay logged at 10:20"), 1));
    rig.advance(300ms);
    QQuickItem* card = rig.find(QStringLiteral("toastCard"));
    REQUIRE(card != nullptr);
    CHECK(toast->isVisible());
    CHECK(card->opacity() == 1.0);
    QSignalSpy announced(toast, SIGNAL(announced(QString)));
    auto* fadeOut = object_named(toast, QStringLiteral("toastFadeOut"));
    auto* fadeIn = object_named(toast, QStringLiteral("toastFadeIn"));

    toast->setProperty("text", QStringLiteral("Mute Delay logged ×2 at 10:21"));
    toast->setProperty("generation", 2);
    for (int frame = 0; frame < 10; ++frame)
    {
        rig.advance(16ms);
        // At no frame is it hidden, fading or sliding again.
        CHECK(toast->isVisible());
        CHECK(card->opacity() == 1.0);
        CHECK_FALSE(fadeOut->property("running").toBool());
        CHECK_FALSE(fadeIn->property("running").toBool());
    }
    CHECK(require_item(rig, QStringLiteral("toastText"))->property("text").toString() ==
          QStringLiteral("Mute Delay logged ×2 at 10:21"));
    CHECK(rig.find(QStringLiteral("toastCard")) == card);

    // The new line is announced once, and the bar is a new window.
    REQUIRE(announced.count() == 1);
    CHECK(announced.at(0).at(0).toString() == QStringLiteral("Mute Delay logged ×2 at 10:21"));
    CHECK(toast->property("windowStarts").toInt() == 2);
}

TEST_CASE("toast.lifecycle: in and out, and announced once", "[ui][toast]")
{
    UiRig rig;
    QQuickItem* toast = load_toast(rig, toast_properties(false, QString(), 0));
    rig.advance(300ms);
    QSignalSpy announced(toast, SIGNAL(announced(QString)));
    QQuickItem* card = rig.find(QStringLiteral("toastCard"));
    REQUIRE(card != nullptr);
    const int toast_ms = theme_group(rig, "motion")->property("toastMs").toInt();
    CHECK(toast_ms == 240);

    // Closed: nothing shows, nothing is read.
    CHECK_FALSE(toast->isVisible());
    CHECK(announced.count() == 0);
    bool in_tree = false;
    for (const Node& node : rig.accessible_tree())
    {
        in_tree = in_tree || node.m_ObjectName == QStringLiteral("toastUndo");
    }
    CHECK_FALSE(in_tree);

    // It opens: the card is shown at once and has slid in and faded in by the end of the animation.
    toast->setProperty("text", QStringLiteral("Mute Delay logged at 10:20"));
    toast->setProperty("generation", 1);
    toast->setProperty("active", true);
    rig.advance(16ms);
    CHECK(toast->isVisible());
    CHECK(object_named(toast, QStringLiteral("toastFadeIn"))->property("running").toBool());
    advance_through(rig, toast_ms);
    CHECK(card->opacity() == 1.0);
    CHECK(card->y() == 0.0);
    CHECK(announced.count() == 1);
    CHECK(announced.at(0).at(0).toString() == QStringLiteral("Mute Delay logged at 10:20"));
    // Staying open does not say it again.
    rig.advance(2000ms);
    CHECK(announced.count() == 1);

    // It closes (the window ended or the undo committed): faded out and gone by the end of the animation.
    toast->setProperty("active", false);
    rig.advance(16ms);
    CHECK(toast->isVisible());
    CHECK(object_named(toast, QStringLiteral("toastFadeOut"))->property("running").toBool());
    advance_through(rig, toast_ms);
    CHECK(card->opacity() == 0.0);
    CHECK_FALSE(toast->isVisible());

    // And it opens again for the next action, announced again.
    toast->setProperty("text", QStringLiteral("Note added at 10:30"));
    toast->setProperty("generation", 3);
    toast->setProperty("active", true);
    advance_through(rig, toast_ms);
    CHECK(toast->isVisible());
    CHECK(announced.count() == 2);
    CHECK(announced.at(1).at(0).toString() == QStringLiteral("Note added at 10:30"));
}

TEST_CASE("toast.lifecycle: with Reduce Motion it fades and does not move", "[ui][toast][motion]")
{
    UiRig rig;
    rig.set_reduce_motion(true);
    QQuickItem* toast = load_toast(rig, toast_properties(false, QString(), 0));
    rig.advance(300ms);
    QQuickItem* card = rig.find(QStringLiteral("toastCard"));

    toast->setProperty("text", QStringLiteral("Mute Delay logged at 10:20"));
    toast->setProperty("generation", 1);
    toast->setProperty("active", true);
    rig.advance(16ms);
    advance_through(rig, 100);
    CHECK(card->opacity() == 1.0);
    CHECK(card->y() == 0.0);
    CHECK(object_named(toast, QStringLiteral("toastSlideIn"))->property("from").toReal() == 0.0);
    CHECK(object_named(toast, QStringLiteral("toastFadeIn"))->property("duration").toInt() == 100);
    expect_clean(check_motion_reduced(rig));
}

TEST_CASE("toast.a11y: names, 44 pt targets, reading order and the text scales, in light and dark", "[ui][toast][a11y]")
{
    UiRig rig;
    for (const bool dark : {false, true})
    {
        rig.set_dark(dark);
        QQuickItem* toast =
            load_toast(rig, toast_properties(true, QStringLiteral("Wrong Response logged ×3 at 10:20"), 1));
        rig.advance(300ms);
        INFO((dark ? "dark" : "light"));
        expect_clean(check_everything(rig));

        // At the largest text size the card grows to hold the line instead of cutting it off.
        rig.set_text_scale(3.1);
        rig.advance(16ms);
        toast->setHeight(toast->implicitHeight());
        const QQuickItem* line = rig.find(QStringLiteral("toastText"));
        CHECK_FALSE(line->property("truncated").toBool());
        CHECK(toast->implicitHeight() > 52);
        CHECK(line->property("contentHeight").toDouble() <= line->height() + 0.5);
        rig.set_text_scale(1.0);
    }
}

TEST_CASE("toast.shield: a touch on the toast never reaches the tile under it, and Undo still works", "[ui][toast]")
{
    UiRig rig;
    // A tile under the whole card, as the toast floats over the bottom row of the board.
    QQuickItem* wrapper = rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            id: wrapper
            property bool shown: true
            property int undone: 0
            Tile {
                objectName: "under"
                width: 370
                height: 104
                typeId: "mute-delay"
                label: "Mute Delay"
                colourSlot: 8
                count: 0
                hasCount: false
                isOther: false
                isEmpty: false
                accessibleName: "Mute Delay"
            }
            UndoToast {
                objectName: "toast"
                width: 370
                active: wrapper.shown
                text: "Mute Delay logged at 10:20"
                undoing: false
                generation: 1
                windowMs: 5000
                onUndoRequested: wrapper.undone += 1
            }
        })"));
    REQUIRE(wrapper != nullptr);
    QQuickItem* under = rig.find_item(QStringLiteral("under"));
    QQuickItem* toast = rig.find_item(QStringLiteral("toast"));
    REQUIRE(under != nullptr);
    REQUIRE(toast != nullptr);
    advance_through(rig, 240);
    REQUIRE(toast->isVisible());
    QSignalSpy pressed(under, SIGNAL(pressed()));
    QSignalSpy tapped(under, SIGNAL(tapped()));

    // On the text, on the bare card at its left edge and on the card away from any control.
    QQuickItem* text = rig.find_item(QStringLiteral("toastText"));
    REQUIRE(text != nullptr);
    rig.tap(text);
    rig.tap(rig.find_item(QStringLiteral("toastCard")), QPointF(3, 26));
    CHECK(pressed.count() == 0);
    CHECK(tapped.count() == 0);

    // A press alone, held, goes nowhere either; and neither does a press that slides off the card.
    const int finger = rig.press(text);
    rig.advance(48ms);
    CHECK(pressed.count() == 0);
    rig.release(finger);
    CHECK(tapped.count() == 0);

    // Undo asks once, and does not press the tile either.
    QQuickItem* button = rig.find_item(QStringLiteral("toastUndo"));
    REQUIRE(button != nullptr);
    rig.tap(button);
    CHECK(wrapper->property("undone").toInt() == 1);
    CHECK(pressed.count() == 0);
    CHECK(tapped.count() == 0);

    // The card is not a control of its own: the only button in the toast is Undo.
    int buttons = 0;
    for (const Node& node : rig.accessible_tree(toast))
    {
        if (node.m_Role == QAccessible::Button && node.m_InAccessibleTree)
        {
            ++buttons;
            CHECK(node.m_ObjectName == QStringLiteral("toastUndo"));
        }
    }
    CHECK(buttons == 1);

    // Hidden, the tile under it takes touches again.
    wrapper->setProperty("shown", false);
    advance_through(rig, 240);
    REQUIRE_FALSE(toast->isVisible());
    rig.tap(under, QPointF(200, 26));
    CHECK(pressed.count() == 1);
    CHECK(tapped.count() == 1);
}
