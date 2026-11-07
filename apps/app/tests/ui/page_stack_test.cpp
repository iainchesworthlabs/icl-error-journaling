// SPDX-License-Identifier: GPL-3.0-or-later
// PageStack (docs/plan/stage-4.md, 4.4): page_stack.push_pop, Reduce Motion and the cross-cutting checks.
#include "container_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QByteArray>
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
                color: Theme.colour.background
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
        return find_item(m_Rig, name);
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
        run(f.m_Rig, 100ms);
        CHECK(f.shown(QStringLiteral("pageOne")));
        CHECK(f.shown(QStringLiteral("pageTwo")));

        run(f.m_Rig, 500ms);
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
        run(f.m_Rig, 600ms);
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
        run(f.m_Rig, 400ms);
        CHECK(f.depth() == 1);
        CHECK(f.shown(QStringLiteral("pageOne")));
    }

    SECTION("several pages deep, and back to the root")
    {
        for (int i = 0; i < 3; ++i)
        {
            f.push();
            run(f.m_Rig, 500ms);
        }
        CHECK(f.depth() == 4);
        for (int i = 0; i < 3; ++i)
        {
            f.pop();
            run(f.m_Rig, 500ms);
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
    run(f.m_Rig, 200ms);
    CHECK(f.busy());
    run(f.m_Rig, 300ms);
    CHECK_FALSE(f.busy());
}

TEST_CASE("page_stack.push_pop: the transitions run on Theme.motion: a slide of the stack's width in 280 ms",
          "[ui][page_stack][motion]")
{
    Fixture f;
    CHECK(f.m_Stack->property("slideDistance").toDouble() == 402);
    CHECK(f.m_Stack->property("parallaxDistance").toDouble() == 402.0 / 3);
    CHECK(f.m_Stack->property("transitionMs").toInt() == 280);
    CHECK(f.m_Stack->property("hiddenOpacity").toDouble() == 1); // a slide, not a fade
}

TEST_CASE("page_stack.push_pop: with Reduce Motion nothing travels and the pages fade in 100 ms",
          "[ui][page_stack][motion]")
{
    Fixture f;
    f.m_Rig.set_reduce_motion(true);
    CHECK(f.m_Stack->property("slideDistance").toDouble() == 0);
    CHECK(f.m_Stack->property("parallaxDistance").toDouble() == 0);
    CHECK(f.m_Stack->property("transitionMs").toInt() == 100);
    CHECK(f.m_Stack->property("hiddenOpacity").toDouble() == 0); // a fade
}

TEST_CASE("motion.reduce: with Reduce Motion the pages fade in 100 ms and do not slide", "[ui][page_stack][motion]")
{
    Fixture f;

    SECTION("with Reduce Motion on, a push and a pop end within the time of a fade, and the pages are in place")
    {
        f.m_Rig.set_reduce_motion(true);
        f.push();
        run(f.m_Rig, 50ms);
        CHECK(f.busy());
        run(f.m_Rig, 250ms);
        CHECK_FALSE(f.busy());
        CHECK(f.m_Rig.geometry(f.item(QStringLiteral("pageTwo"))) == QRectF(0, 0, 402, 874));
        f.pop();
        run(f.m_Rig, 300ms);
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
    run(f.m_Rig, 500ms);
    expect_clean(check_everything(f.m_Rig));
}
