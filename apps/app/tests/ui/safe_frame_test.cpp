// SPDX-License-Identifier: GPL-3.0-or-later
// SafeFrame (docs/plan/stage-4.md, "Safe area"): safe_frame.insets and safe_frame.override.
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <QtLogging>

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
