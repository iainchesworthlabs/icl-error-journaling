// SPDX-License-Identifier: GPL-3.0-or-later
// PageStack (docs/plan/stage-4.md, 4.4): page_stack.push_pop, Reduce Motion and the cross-cutting checks.
#include "container_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QByteArray>
#include <QColor>
#include <QImage>
#include <QQuickItem>
#include <chrono>

using namespace ej::ui;
using namespace ej::ui::containers;
using namespace std::chrono_literals;

namespace
{

// Settings, and the page its Locations row pushes.
constexpr auto STACK = R"(
    import QtQuick
    import ErrorJournalingApp
    Item {
        id: host
        property alias stack: stack
        function pushPage() { stack.push(secondPage) }
        function popPage() { stack.pop() }
        function replacePage() { stack.replace(null, secondPage) }
        Component {
            id: firstPage
            Rectangle {
                objectName: "pageOne"
                color: Theme.colour.background
                Rectangle {
                    objectName: "openTwo"
                    x: 20; y: 100; width: 160; height: 54
                    color: Theme.colour.primary
                    Accessible.role: Accessible.Button
                    Accessible.name: "Locations"
                    TapHandler { onTapped: host.pushPage() }
                }
            }
        }
        Component {
            id: secondPage
            Rectangle {
                objectName: "pageTwo"
                color: Theme.colour.primaryTint
                Rectangle {
                    objectName: "back"
                    x: 20; y: 100; width: 160; height: 54
                    color: Theme.colour.primary
                    Accessible.role: Accessible.Button
                    Accessible.name: "Settings"
                    TapHandler { onTapped: host.popPage() }
                }
            }
        }
        PageStack {
            id: stack
            objectName: "stack"
            anchors.fill: parent
            initialItem: firstPage
        }
    })";

struct Fixture
{
    UiRig m_Rig;
    QQuickItem* m_Host = nullptr;
    QQuickItem* m_Stack = nullptr;

    Fixture()
    {
        m_Host = m_Rig.load_source(QByteArray(STACK));
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Host != nullptr);
        m_Stack = m_Rig.find(QStringLiteral("stack"));
        REQUIRE(m_Stack != nullptr);
    }

    /// An item of a page, found through the item tree.
    QQuickItem* item(const QString& name)
    {
        return m_Rig.find_item(name);
    }

    int depth() const
    {
        return m_Stack->property("depth").toInt();
    }

    bool busy() const
    {
        return m_Stack->property("busy").toBool();
    }

    void push()
    {
        QMetaObject::invokeMethod(m_Host, "pushPage");
    }

    void pop()
    {
        QMetaObject::invokeMethod(m_Host, "popPage");
    }

    bool shown(const QString& name)
    {
        QQuickItem* page = item(name);
        return page != nullptr && page->isVisible();
    }
};

} // namespace

TEST_CASE("page_stack.push_pop: a pushed page slides in over the first, and popping brings the first back",
          "[ui][page_stack]")
{
    Fixture f;
    CHECK(f.depth() == 1);
    CHECK(f.shown(QStringLiteral("pageOne")));
    CHECK_FALSE(f.busy());

    SECTION("a tap in the first page pushes the second")
    {
        f.m_Rig.tap(f.item(QStringLiteral("openTwo")));
        CHECK(f.depth() == 2);
        CHECK(f.busy());
        // Both pages are there while the transition runs.
        f.m_Rig.advance(100ms);
        CHECK(f.shown(QStringLiteral("pageOne")));
        CHECK(f.shown(QStringLiteral("pageTwo")));

        f.m_Rig.advance(500ms);
        CHECK_FALSE(f.busy());
        QQuickItem* two = f.item(QStringLiteral("pageTwo"));
        REQUIRE(two != nullptr);
        CHECK(f.m_Rig.geometry(two) == QRectF(0, 0, 402, 874));
        CHECK(two->isVisible());
        CHECK_FALSE(f.shown(QStringLiteral("pageOne")));

        // A screen reader reaches the page in view and not the one under it.
        const std::vector<Node> reached = f.m_Rig.accessible_tree();
        CHECK(find_shown(reached, QStringLiteral("openTwo")) == nullptr);
        CHECK(find_shown(reached, QStringLiteral("back")) != nullptr);

        f.m_Rig.tap(f.item(QStringLiteral("back")));
        CHECK(f.depth() == 1);
        f.m_Rig.advance(600ms);
        CHECK_FALSE(f.busy());
        QQuickItem* one = f.item(QStringLiteral("pageOne"));
        REQUIRE(one != nullptr);
        CHECK(one->isVisible());
        CHECK(f.m_Rig.geometry(one) == QRectF(0, 0, 402, 874));
        CHECK_FALSE(f.shown(QStringLiteral("pageTwo")));
    }

    SECTION("the root page cannot be popped")
    {
        f.pop();
        f.m_Rig.advance(400ms);
        CHECK(f.depth() == 1);
        CHECK(f.shown(QStringLiteral("pageOne")));
    }

    SECTION("several pages deep, and back to the root")
    {
        for (int i = 0; i < 3; ++i)
        {
            f.push();
            f.m_Rig.advance(500ms);
        }
        CHECK(f.depth() == 4);
        for (int i = 0; i < 3; ++i)
        {
            f.pop();
            f.m_Rig.advance(500ms);
        }
        CHECK(f.depth() == 1);
        CHECK(f.shown(QStringLiteral("pageOne")));
    }
}

TEST_CASE("page_stack.push_pop: the transitions take Theme.motion.pageMs", "[ui][page_stack]")
{
    Fixture f;
    f.push();
    // 280 ms plus the frame an animator needs to start: still running at 200 ms, finished well after 400 ms.
    f.m_Rig.advance(200ms);
    CHECK(f.busy());
    f.m_Rig.advance(300ms);
    CHECK_FALSE(f.busy());
}

namespace
{

QObject* transition(QQuickItem* stack, const char* name)
{
    return stack->property(name).value<QObject*>();
}

} // namespace

TEST_CASE("page_stack.push_pop: the transitions run on Theme.motion: a slide of the stack's width in 280 ms",
          "[ui][page_stack][motion]")
{
    Fixture f;
    CHECK(f.m_Stack->property("slideDistance").toDouble() == 402);
    CHECK(f.m_Stack->property("parallaxDistance").toDouble() == 402.0 / 3);
    CHECK(f.m_Stack->property("transitionMs").toInt() == 280);
    CHECK_FALSE(f.m_Stack->property("fading").toBool());
    // Push and pop slide, in both directions, on each of the pages; replace fades.
    CHECK(transition(f.m_Stack, "pushEnter") == transition(f.m_Stack, "slideIn"));
    CHECK(transition(f.m_Stack, "pushExit") == transition(f.m_Stack, "slideUnder"));
    CHECK(transition(f.m_Stack, "popEnter") == transition(f.m_Stack, "slideBack"));
    CHECK(transition(f.m_Stack, "popExit") == transition(f.m_Stack, "slideOut"));
    CHECK(transition(f.m_Stack, "replaceEnter") == transition(f.m_Stack, "fadeIn"));
    CHECK(transition(f.m_Stack, "replaceExit") == transition(f.m_Stack, "fadeOut"));
}

TEST_CASE("page_stack.push_pop: with Reduce Motion nothing slides and the pages fade in 100 ms",
          "[ui][page_stack][motion]")
{
    Fixture f;
    f.m_Rig.set_reduce_motion(true);
    CHECK(f.m_Stack->property("transitionMs").toInt() == 100);
    CHECK(f.m_Stack->property("fading").toBool());
    CHECK(transition(f.m_Stack, "pushEnter") == transition(f.m_Stack, "fadeIn"));
    CHECK(transition(f.m_Stack, "pushExit") == transition(f.m_Stack, "fadeOut"));
    CHECK(transition(f.m_Stack, "popEnter") == transition(f.m_Stack, "fadeIn"));
    CHECK(transition(f.m_Stack, "popExit") == transition(f.m_Stack, "fadeOut"));
    CHECK(transition(f.m_Stack, "replaceEnter") == transition(f.m_Stack, "fadeIn"));
    CHECK(transition(f.m_Stack, "replaceExit") == transition(f.m_Stack, "fadeOut"));
}

TEST_CASE("motion.reduce: with Reduce Motion the pages fade in 100 ms and do not slide", "[ui][page_stack][motion]")
{
    Fixture f;

    SECTION("with Reduce Motion on, a push and a pop end within the time of a fade, and the pages are in place")
    {
        f.m_Rig.set_reduce_motion(true);
        f.push();
        f.m_Rig.advance(50ms);
        CHECK(f.busy());
        f.m_Rig.advance(250ms);
        CHECK_FALSE(f.busy());
        CHECK(f.m_Rig.geometry(f.item(QStringLiteral("pageTwo"))) == QRectF(0, 0, 402, 874));
        f.pop();
        f.m_Rig.advance(300ms);
        CHECK_FALSE(f.busy());
        CHECK(f.depth() == 1);
        CHECK(f.m_Rig.geometry(f.item(QStringLiteral("pageOne"))) == QRectF(0, 0, 402, 874));
    }
}

TEST_CASE("page_stack: each page passes the cross-cutting checks, in light and dark", "[ui][page_stack]")
{
    Fixture f;
    const bool dark = GENERATE(false, true);
    f.m_Rig.set_dark(dark);
    expect_clean(check_everything(f.m_Rig));
    f.push();
    f.m_Rig.advance(500ms);
    expect_clean(check_everything(f.m_Rig));
}

namespace
{

/// The colour of the window at a point of the middle of the stack, away from any button.
QColor pixel_at_middle(UiRig& rig)
{
    return rig.grab().pixelColor(200, 500);
}

} // namespace

TEST_CASE("page_stack.opacity: after a push or a pop the page in view is opaque, visible and painted",
          "[ui][page_stack]")
{
    Fixture f;
    const bool reduce = GENERATE(false, true);
    const bool dark = GENERATE(false, true);
    INFO("reduce motion " << reduce << ", dark " << dark);
    f.m_Rig.set_dark(dark);
    f.m_Rig.set_reduce_motion(reduce);
    f.m_Rig.advance(100ms);
    const QColor first = pixel_at_middle(f.m_Rig);

    for (int round = 0; round < 2; ++round)
    {
        INFO("round " << round);
        // Pushing: the new page is the top one and nothing of it is left transparent.
        f.m_Rig.tap(f.item(QStringLiteral("openTwo")));
        f.m_Rig.advance(600ms);
        REQUIRE_FALSE(f.busy());
        QQuickItem* two = f.item(QStringLiteral("pageTwo"));
        REQUIRE(two != nullptr);
        CHECK(two->isVisible());
        CHECK(two->opacity() == 1);
        CHECK(f.m_Stack->property("currentItem").value<QQuickItem*>() == two);
        // A grab of the window shows the second page's colour, and not the first page's or an empty window.
        const QColor second = pixel_at_middle(f.m_Rig);
        CHECK(second != first);
        CHECK(second.alpha() == 255);
        CHECK(second == theme_colour(f.m_Rig, "primaryTint"));

        // Popping: the first page is back, opaque and painted.
        f.m_Rig.tap(f.item(QStringLiteral("back")));
        f.m_Rig.advance(600ms);
        REQUIRE_FALSE(f.busy());
        QQuickItem* one = f.item(QStringLiteral("pageOne"));
        REQUIRE(one != nullptr);
        CHECK(one->isVisible());
        CHECK(one->opacity() == 1);
        CHECK(f.m_Stack->property("currentItem").value<QQuickItem*>() == one);
        CHECK(pixel_at_middle(f.m_Rig) == first);
    }
}

TEST_CASE("page_stack.opacity: a replace ends with the new page opaque and visible", "[ui][page_stack]")
{
    Fixture f;
    const bool reduce = GENERATE(false, true);
    f.m_Rig.set_reduce_motion(reduce);
    QMetaObject::invokeMethod(f.m_Host, "replacePage");
    f.m_Rig.advance(600ms);
    REQUIRE_FALSE(f.busy());
    QQuickItem* two = f.item(QStringLiteral("pageTwo"));
    REQUIRE(two != nullptr);
    CHECK(two->isVisible());
    CHECK(two->opacity() == 1);
    CHECK(f.depth() == 1);
}

TEST_CASE("page_stack.keyboard: a page that is left takes the focus of its field and the keyboard with it",
          "[ui][page_stack]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            id: host
            function pushEditor() { stack.push(editor) }
            function popPage() { stack.pop() }
            Component {
                id: first
                Rectangle { objectName: "pageOne"; color: Theme.colour.background }
            }
            Component {
                id: editor
                Rectangle {
                    color: Theme.colour.background
                    TextInput {
                        objectName: "digits"
                        x: 20; y: 100; width: 200; height: 40
                        inputMethodHints: Qt.ImhDigitsOnly
                    }
                }
            }
            PageStack {
                id: stack
                anchors.fill: parent
                initialItem: first
            }
        })")) != nullptr);
    QQuickItem* host = rig.root();

    SECTION("popping the page that holds the field with the number pad up")
    {
        QMetaObject::invokeMethod(host, "pushEditor");
        rig.advance(600ms);
        QQuickItem* digits = rig.find_item(QStringLiteral("digits"));
        REQUIRE(digits != nullptr);
        digits->forceActiveFocus();
        rig.show_keyboard(301);
        REQUIRE(rig.focused_input() == digits);
        QMetaObject::invokeMethod(host, "popPage");
        rig.advance(600ms);
        CHECK(rig.focused_input() == nullptr);
        CHECK_FALSE(rig.keyboard_up());
    }

    SECTION("pushing another page over the page that holds the field")
    {
        QMetaObject::invokeMethod(host, "pushEditor");
        rig.advance(600ms);
        QQuickItem* digits = rig.find_item(QStringLiteral("digits"));
        REQUIRE(digits != nullptr);
        digits->forceActiveFocus();
        rig.show_keyboard(301);
        QMetaObject::invokeMethod(host, "pushEditor");
        rig.advance(600ms);
        CHECK(rig.focused_input() == nullptr);
        CHECK_FALSE(rig.keyboard_up());
    }
}
