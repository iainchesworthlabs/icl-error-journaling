// SPDX-License-Identifier: GPL-3.0-or-later
// SafeFrame (docs/plan/stage-4.md, "Safe area"): safe_frame.insets and safe_frame.override.
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QtLogging>
#include <chrono>

using namespace ej::ui;

namespace
{

constexpr auto FRAME = R"(
    import QtQuick
    import ErrorJournalingApp
    SafeFrame {
        id: frame
        objectName: "frame"
        overrideInsets: true
        overrideTop: 59
        overrideBottom: 34
        Rectangle {
            objectName: "inside"
            anchors.fill: parent
        }
    })";

} // namespace

TEST_CASE("safe_frame.insets: with no override it reads the platform's insets, which an offscreen window has none of",
          "[ui][safe_frame]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        SafeFrame {
            objectName: "frame"
            Rectangle { objectName: "inside"; anchors.fill: parent }
        })")) != nullptr);
    QQuickItem* frame = rig.find(QStringLiteral("frame"));
    CHECK(frame->property("topInset").toDouble() == 0);
    CHECK(frame->property("bottomInset").toDouble() == 0);
    CHECK(frame->property("leftInset").toDouble() == 0);
    CHECK(frame->property("rightInset").toDouble() == 0);
    CHECK(rig.geometry(rig.find(QStringLiteral("inside"))) == QRectF(0, 0, 402, 874));
}

TEST_CASE("safe_frame.override: the frame is the window minus the insets given", "[ui][safe_frame]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArray(FRAME)) != nullptr);
    QQuickItem* frame = rig.find(QStringLiteral("frame"));
    QQuickItem* inside = rig.find(QStringLiteral("inside"));
    REQUIRE(inside != nullptr);

    SECTION("the design's iPhone 17: 59 pt at the top and 34 pt at the bottom")
    {
        CHECK(frame->property("topInset").toDouble() == 59);
        CHECK(frame->property("bottomInset").toDouble() == 34);
        CHECK(rig.geometry(inside) == QRectF(0, 59, 402, 874 - 59 - 34));
        CHECK(inside->parentItem() == frame->property("frame").value<QQuickItem*>());
    }

    SECTION("a different phone: the same plus 20 pt")
    {
        frame->setProperty("overrideTop", 79);
        frame->setProperty("overrideBottom", 54);
        rig.layout();
        CHECK(rig.geometry(inside) == QRectF(0, 79, 402, 874 - 79 - 54));
    }

    SECTION("insets at the sides")
    {
        frame->setProperty("overrideLeft", 12);
        frame->setProperty("overrideRight", 8);
        rig.layout();
        CHECK(rig.geometry(inside) == QRectF(12, 59, 402 - 12 - 8, 874 - 59 - 34));
    }

    SECTION("turning the override off goes back to the platform")
    {
        frame->setProperty("overrideInsets", false);
        rig.layout();
        CHECK(rig.geometry(inside) == QRectF(0, 0, 402, 874));
    }
}

TEST_CASE("safe_frame.override: the frame follows the size of the window", "[ui][safe_frame]")
{
    UiRig rig(QSize(430, 932));
    REQUIRE(rig.load_source(QByteArray(FRAME)) != nullptr);
    CHECK(rig.geometry(rig.find(QStringLiteral("inside"))) == QRectF(0, 59, 430, 932 - 59 - 34));
}

TEST_CASE("safe_frame.log: logInsets prints the four insets once, for the device build of assumption U1",
          "[ui][safe_frame]")
{
    static QStringList messages;
    messages.clear();
    const auto previous = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& text)
                                                 { messages.append(text); });
    {
        UiRig rig;
        REQUIRE(rig.load_source(QByteArrayLiteral(R"(
            import QtQuick
            import ErrorJournalingApp
            SafeFrame {
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
                logInsets: true
            })")) != nullptr);
    }
    qInstallMessageHandler(previous);
    INFO(messages.join(QStringLiteral(" | ")).toStdString());
    REQUIRE(messages.size() == 1);
    CHECK(messages.front() == QStringLiteral("safe area: top 59, bottom 34, left 0, right 0"));
}

TEST_CASE("safe_frame.override: content in the frame passes the cross-cutting checks at both pairs of insets",
          "[ui][safe_frame][a11y]")
{
    UiRig rig;
    // The stub puts its content in a frame with the design's insets.
    REQUIRE(rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("Today's details")}}) !=
            nullptr);
    expect_clean(check_safe_area(rig, 59, 34));
    expect_clean(check_everything(rig));
}

namespace
{

constexpr auto KEYBOARD_FRAME = R"(
    import QtQuick
    import ErrorJournalingApp
    SafeFrame {
        id: frame
        objectName: "frame"
        avoidKeyboard: true
        Flickable {
            objectName: "flick"
            anchors.fill: parent
            contentWidth: width
            contentHeight: 1600
            clip: true
            Item {
                width: parent.width
                height: 1600
                TextInput {
                    objectName: "high"
                    y: 100
                    width: 300
                    height: 40
                    inputMethodHints: Qt.ImhDigitsOnly
                }
                TextInput {
                    objectName: "low"
                    y: 1200
                    width: 300
                    height: 40
                }
            }
        }
    })";

} // namespace

TEST_CASE("safe_frame.keyboard: on the full-screen window with its real insets the frame ends at the keyboard and the "
          "top does not move",
          "[ui][safe_frame]")
{
    constexpr double KEYBOARD = 301.0;
    UiRig rig;
    rig.set_safe_area(62, 34);
    REQUIRE(rig.load_source(QByteArray(KEYBOARD_FRAME)) != nullptr);
    QQuickItem* frame = rig.find(QStringLiteral("frame"));
    QQuickItem* inner = rig.find(QStringLiteral("safeFrameInner"));
    REQUIRE(frame != nullptr);
    REQUIRE(inner != nullptr);
    CHECK(frame->property("topInset").toDouble() == 62);
    CHECK(frame->property("bottomInset").toDouble() == 34);
    CHECK(rig.geometry(inner) == QRectF(0, 62, 402, 874 - 62 - 34));
    CHECK(frame->property("keyboardInset").toDouble() == 0);

    SECTION("it ends at the top of the keyboard, which replaces the home indicator inset")
    {
        rig.show_keyboard(KEYBOARD);
        CHECK(frame->property("keyboardInset").toDouble() == KEYBOARD);
        CHECK(rig.geometry(inner) == QRectF(0, 62, 402, 874 - KEYBOARD - 62));
        // The status bar and the Dynamic Island stay clear, and the home indicator inset is back with the keyboard
        // gone.
        rig.hide_keyboard();
        CHECK(rig.geometry(inner) == QRectF(0, 62, 402, 874 - 62 - 34));
    }

    SECTION("the start of the transition already ends the frame at the keyboard")
    {
        rig.show_keyboard_starting(KEYBOARD);
        CHECK(rig.geometry(inner).bottom() == 874 - KEYBOARD);
    }

    SECTION("a keyboard that is lower than the home indicator inset leaves the frame where it is")
    {
        rig.show_keyboard(20);
        CHECK(rig.geometry(inner).bottom() == 874 - 34);
    }

    SECTION("without avoidKeyboard the keyboard is ignored")
    {
        frame->setProperty("avoidKeyboard", false);
        rig.show_keyboard(KEYBOARD);
        CHECK(rig.geometry(inner) == QRectF(0, 62, 402, 874 - 62 - 34));
    }
}

TEST_CASE(
    "safe_frame.reveal: the field being typed in is scrolled into view in the Flickable that holds it, clear of the "
    "keyboard and of the top inset",
    "[ui][safe_frame]")
{
    constexpr double KEYBOARD = 301.0;
    UiRig rig;
    rig.set_safe_area(62, 34);
    REQUIRE(rig.load_source(QByteArray(KEYBOARD_FRAME)) != nullptr);
    QQuickItem* flick = rig.find(QStringLiteral("flick"));
    QQuickItem* low = rig.find(QStringLiteral("low"));
    QQuickItem* high = rig.find(QStringLiteral("high"));
    REQUIRE(low != nullptr);
    REQUIRE(high != nullptr);

    low->forceActiveFocus();
    rig.show_keyboard(KEYBOARD);
    rig.advance(std::chrono::milliseconds(100));
    CHECK(rig.geometry(flick).bottom() == 874 - KEYBOARD);
    CHECK(rig.geometry(low).bottom() <= 874 - KEYBOARD);
    CHECK(rig.geometry(low).top() >= 62);

    // Back to a field at the top of the page: the page scrolls up to it, and does not leave it under the top inset.
    high->forceActiveFocus();
    rig.advance(std::chrono::milliseconds(100));
    CHECK(rig.geometry(high).top() >= 62);
    CHECK(rig.geometry(high).bottom() <= 874 - KEYBOARD);
}
