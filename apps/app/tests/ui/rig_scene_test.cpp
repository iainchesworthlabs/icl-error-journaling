// SPDX-License-Identifier: GPL-3.0-or-later
// The rig's handling of frames, touch updates, the item tree and the scene, and the checks that depend on them.
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
#include <chrono>

using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

bool any_line_has(const Violations& found, const QString& text)
{
    return std::ranges::any_of(found, [&text](const QString& line) { return line.contains(text); });
}

} // namespace

TEST_CASE("rig.advance: an animator is finished after its duration, in one long advance", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            Rectangle {
                objectName: "scaled"
                width: 50; height: 50
                ScaleAnimator on scale { from: 1; to: 0.5; duration: 120 }
            }
            Rectangle {
                objectName: "moved"
                y: 100; width: 50; height: 50
                XAnimator on x { from: 0; to: 200; duration: 240 }
            }
            Rectangle {
                objectName: "faded"
                y: 200; width: 50; height: 50
                OpacityAnimator on opacity { from: 1; to: 0.25; duration: 120 }
            }
        })")) != nullptr);
    QQuickItem* scaled = rig.find(QStringLiteral("scaled"));
    QQuickItem* moved = rig.find(QStringLiteral("moved"));
    QQuickItem* faded = rig.find(QStringLiteral("faded"));
    REQUIRE(scaled != nullptr);

    // An animator writes its value back when it ends, so nothing is seen half way; it has not ended yet.
    rig.advance(48ms);
    CHECK(scaled->scale() == 1.0);
    CHECK(moved->x() == 0);

    // One long advance finishes the rest: the animators started on the first frame, and the span covers their duration.
    rig.advance(400ms);
    CHECK(scaled->scale() == 0.5);
    CHECK(moved->x() == 200);
    CHECK(faded->opacity() == 0.25);
}

TEST_CASE("rig.wait_real: a QML Timer runs on the wall clock, which advance does not move", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            id: root
            property int ticks: 0
            Timer { interval: 40; repeat: true; running: true; onTriggered: root.ticks += 1 }
        })")) != nullptr);
    // Ten seconds of rig time is not ten seconds of the timer's: it ticks with the real time the frames take, which is
    // far less than the 250 ticks that a clock moved by the rig would give.
    rig.advance(10s);
    const int after_advance = rig.root()->property("ticks").toInt();
    CHECK(after_advance < 100);
    rig.wait_real(250ms);
    CHECK(rig.root()->property("ticks").toInt() >= after_advance + 2);
}

TEST_CASE("rig.move: every update of a finger arrives, in order, and the last position is not lost", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            id: root
            property int moves: 0
            property real lastX: -1
            property real lastY: -1
            property bool released: false
            MouseArea {
                objectName: "area"
                x: 20; y: 100; width: 300; height: 300
                onPositionChanged: (mouse) => { root.moves += 1; root.lastX = mouse.x; root.lastY = mouse.y }
                onReleased: root.released = true
            }
        })")) != nullptr);
    QQuickItem* area = rig.find(QStringLiteral("area"));
    QQuickItem* root = rig.root();
    REQUIRE(area != nullptr);

    const int finger = rig.press(area, QPointF(10, 10));
    for (int step = 1; step <= 10; ++step)
    {
        rig.move(finger, area, QPointF(10 + step * 20, 10 + step * 10));
        // Delivered before move() returned: not at the next frame, not after a second send.
        CHECK(root->property("moves").toInt() == step);
        CHECK(root->property("lastX").toDouble() == 10 + step * 20);
        CHECK(root->property("lastY").toDouble() == 10 + step * 10);
    }
    rig.release(finger);
    CHECK(root->property("released").toBool());
    CHECK(root->property("moves").toInt() == 10);
}

TEST_CASE("rig.find_item: finds the delegates of a Repeater, a ListView and a StackView", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import QtQuick.Controls.Basic
        Item {
            Column {
                Repeater {
                    model: 3
                    Rectangle { required property int index; objectName: "row" + index; width: 50; height: 20 }
                }
                Repeater {
                    model: 2
                    Rectangle { objectName: "same"; width: 50; height: 20 }
                }
            }
            ListView {
                y: 200; width: 100; height: 100
                model: 2
                delegate: Rectangle { required property int index; objectName: "cell" + index; width: 50; height: 20 }
            }
            StackView {
                y: 400; width: 100; height: 100
                initialItem: Rectangle { objectName: "page"; width: 50; height: 20 }
            }
        })")) != nullptr);
    CHECK(rig.find_item(QStringLiteral("row2")) != nullptr);
    CHECK(rig.find_item(QStringLiteral("cell1")) != nullptr);
    CHECK(rig.find_item(QStringLiteral("page")) != nullptr);
    CHECK(rig.find_item(QStringLiteral("row3")) == nullptr);
    CHECK(rig.find_items(QStringLiteral("same")).size() == 2);
    CHECK(rig.find_items(QStringLiteral("nothing")).empty());
    // From a given item.
    QQuickItem* row2 = rig.find_item(QStringLiteral("row2"));
    CHECK(rig.find_item(QStringLiteral("row2"), row2) == row2);
    CHECK(rig.find_item(QStringLiteral("row1"), row2) == nullptr);
}

TEST_CASE("a11y.names: a StackView with an Accessible.name is a region, not a nameless control", "[ui][a11y]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import QtQuick.Controls.Basic
        Item {
            StackView {
                objectName: "pages"
                x: 20; y: 100; width: 300; height: 200
                Accessible.role: Accessible.Pane
                Accessible.name: "Settings pages"
                initialItem: Rectangle {
                    width: 300; height: 200
                    Rectangle {
                        objectName: "go"
                        width: 120; height: 54
                        Accessible.role: Accessible.Button
                        Accessible.name: "Go"
                        TapHandler {}
                    }
                }
            }
        })")) != nullptr);
    expect_clean(check_names(rig));
    const auto nodes = rig.tree();
    const auto pages = std::ranges::find_if(nodes, [](const Node& node) { return node.m_ObjectName == "pages"; });
    REQUIRE(pages != nodes.end());
    CHECK_FALSE(pages->m_Interactive);
}

TEST_CASE("a11y.reading_order: HideBase takes the base layer out, so a sheet over it can be checked", "[ui][a11y]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            // The sheet is above the screen in the stack and so is read first; the screen's title is higher on the page.
            Rectangle {
                objectName: "sheet"
                x: 0; y: 300; width: 402; height: 400
                Text {
                    x: 20; y: 320; text: "Edit entry"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: "Edit entry"
                }
            }
            Item {
                objectName: "base"
                Text {
                    x: 20; y: 60; text: "Review"
                    Accessible.role: Accessible.StaticText
                    Accessible.name: "Review"
                }
            }
        })")) != nullptr);
    QQuickItem* base = rig.find(QStringLiteral("base"));
    REQUIRE(base != nullptr);
    CHECK_FALSE(check_reading_order(rig).empty());
    {
        HideBase hidden(base);
        CHECK_FALSE(base->isVisible());
        expect_clean(check_reading_order(rig));
    }
    CHECK(base->isVisible());
    CHECK_FALSE(check_reading_order(rig).empty());
}

TEST_CASE("a11y.text_scale: a Text cut off by its line count is found, an eliding one is allowed", "[ui][a11y]")
{
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubBadTruncate")) != nullptr);
    // The default Text has elide: ElideNone, which is 3 and not 0; the check must treat it as not eliding.
    QQuickItem* cut = rig.find(QStringLiteral("cut"));
    REQUIRE(cut != nullptr);
    CHECK(cut->property("truncated").toBool());
    CHECK(cut->property("elide").toInt() == 3);
    const Violations found = check_text_scale(rig);
    INFO(describe(found).toStdString());
    CHECK(any_line_has(found, QStringLiteral("text is cut off")));

    UiRig other;
    REQUIRE(other.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            Text {
                objectName: "elided"
                x: 20; y: 100; width: 100
                maximumLineCount: 1
                wrapMode: Text.WordWrap
                elide: Text.ElideRight
                text: "A long note that cannot fit on one line of a hundred points"
            }
        })")) != nullptr);
    CHECK(other.find(QStringLiteral("elided"))->property("truncated").toBool());
    expect_clean(check_text_scale(other));
}

TEST_CASE("perf.scene: delegates are counted however they are declared", "[ui][perf]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            Column {
                Repeater {
                    model: 5
                    Rectangle { width: 10; height: 10 }
                }
                Repeater {
                    model: 2
                    Rectangle { required property int index; required property var model; width: 10; height: 10 }
                }
            }
            ListView {
                y: 300; width: 100; height: 200
                model: 3
                delegate: Rectangle { width: 50; height: 20 }
            }
            Rectangle { width: 10; height: 10 }
        })")) != nullptr);
    const SceneSize size = measure_scene(rig);
    INFO("items " << size.m_Items << ", delegates " << size.m_Delegates);
    CHECK(size.m_Delegates == 5 + 2 + 3);
    CHECK(size.m_Items > size.m_Delegates);
}
