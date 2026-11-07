// SPDX-License-Identifier: GPL-3.0-or-later
// The rig itself (docs/plan/stage-4.md, "Tests": rig.*) and the cross-cutting checks, each against a stub that follows
// the rule and a stub that breaks it, because a check that cannot fail proves nothing.
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_clock.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QQuickItem>
#include <QQuickWindow>
#include <algorithm>
#include <chrono>

using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

QQuickItem* load_good(UiRig& rig)
{
    return rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("Today's details")}});
}

bool any_line_has(const Violations& found, const QString& text)
{
    return std::ranges::any_of(found, [&text](const QString& line) { return line.contains(text); });
}

} // namespace

TEST_CASE("rig.load: a component of the module is loaded with its required properties", "[ui][rig]")
{
    UiRig rig;
    QQuickItem* item = load_good(rig);
    INFO(rig.errors().toStdString());
    REQUIRE(item != nullptr);
    CHECK(rig.root() == item);
    CHECK(rig.geometry(item).size() == QSizeF(402, 874));
    CHECK(rig.find(QStringLiteral("activate")) != nullptr);
    CHECK(rig.find(QStringLiteral("no-such-item")) == nullptr);
}

TEST_CASE("rig.load: a type that does not exist, or a required property left out, is an error and not a crash",
          "[ui][rig]")
{
    UiRig rig;
    CHECK(rig.load(QStringLiteral("NoSuchType")) == nullptr);
    CHECK_FALSE(rig.errors().isEmpty());
    // `title` is required.
    CHECK(rig.load(QStringLiteral("UiStubGood")) == nullptr);
    CHECK_FALSE(rig.errors().isEmpty());
}

TEST_CASE("rig.tree: the walker finds a known control with its role and name", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(load_good(rig) != nullptr);
    const std::vector<Node> nodes = rig.tree();
    REQUIRE_FALSE(nodes.empty());
    CHECK(nodes.front().m_Item == rig.root());

    const auto button = std::ranges::find_if(nodes, [](const Node& node) { return node.m_ObjectName == "activate"; });
    REQUIRE(button != nodes.end());
    CHECK(button->m_Interactive);
    CHECK(button->m_Visible);
    CHECK(button->m_InAccessibleTree);
    CHECK(button->m_Role == QAccessible::Button);
    CHECK(button->m_Name == QStringLiteral("Add"));
    CHECK(button->m_Rect.height() == 54);

    const auto title = std::ranges::find_if(nodes, [](const Node& node) { return node.m_ObjectName == "title"; });
    REQUIRE(title != nodes.end());
    CHECK_FALSE(title->m_Interactive);
    CHECK(title->m_Name == QStringLiteral("Today's details"));
}

TEST_CASE("rig.geometry: positions are in points of the window", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(load_good(rig) != nullptr);
    const QRectF button = rig.geometry(rig.find(QStringLiteral("activate")));
    // Inside the 16 pt margin, below the 59 pt inset and the title.
    CHECK(button.left() == 16);
    CHECK(button.width() == 402 - 2 * 16);
    CHECK(button.top() >= 59);
}

TEST_CASE("rig.press: a press is seen at touch-down and not at release, and fingers are independent", "[ui][rig]")
{
    UiRig rig;
    QQuickItem* item = load_good(rig);
    REQUIRE(item != nullptr);
    QQuickItem* button = rig.find(QStringLiteral("activate"));

    const int finger = rig.press(button);
    CHECK(item->property("presses").toInt() == 1); // before the finger is lifted
    rig.release(finger);
    CHECK(item->property("presses").toInt() == 1);

    rig.tap(button);
    CHECK(item->property("presses").toInt() == 2);

    // Two fingers down, then lifted in the other order.
    const int first = rig.press(button);
    const int second = rig.press(button);
    CHECK(first != second);
    rig.release(first);
    rig.release(second);
    rig.release(second); // a finger already lifted is ignored
}

TEST_CASE("rig.drag: a drag moves a draggable item along the path of the finger", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            Rectangle {
                objectName: "box"
                x: 20; y: 100; width: 100; height: 100
                DragHandler {}
            }
        })")) != nullptr);
    QQuickItem* box = rig.find(QStringLiteral("box"));
    REQUIRE(box != nullptr);
    rig.drag(box, QPointF(50, 50), QPointF(150, 50));
    CHECK(box->x() > 90);
    CHECK(box->y() == 100);
}

TEST_CASE("rig.type: characters reach the focused item", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            TextInput { objectName: "field"; width: 200; height: 40 }
        })")) != nullptr);
    rig.window().requestActivate();
    QQuickItem* field = rig.find(QStringLiteral("field"));
    REQUIRE(field != nullptr);
    field->forceActiveFocus();
    rig.type(QStringLiteral("Sam Example"));
    CHECK(field->property("text").toString() == QStringLiteral("Sam Example"));
    rig.key(Qt::Key_Backspace);
    CHECK(field->property("text").toString() == QStringLiteral("Sam Exampl"));
}

TEST_CASE("rig.advance: an animation is finished after its duration and not before, with no waiting", "[ui][rig]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            Rectangle {
                objectName: "box"
                width: 10; height: 10
                NumberAnimation on x { from: 0; to: 120; duration: 120 }
            }
        })")) != nullptr);
    QQuickItem* box = rig.find(QStringLiteral("box"));
    rig.advance(1ms);
    CHECK(box->x() < 120);
    rig.advance(60ms);
    CHECK(box->x() > 0);
    CHECK(box->x() < 120);
    rig.advance(60ms);
    CHECK(box->x() == 120);
}

TEST_CASE("rig.advance: the clock and the gateway of Stage 3 move with the rig", "[ui][rig]")
{
    ej::test::TestClock clock;
    ej::test::MemoryStorage storage(clock);
    ej::test::ManualStorageGateway gateway(storage.context());
    UiRig rig;
    rig.attach(clock);
    rig.attach(gateway);

    const auto before = clock.monotonic_now();
    rig.advance(5s);
    CHECK(clock.monotonic_now() - before == 5s);

    bool done = false;
    gateway.post([](ej::viewmodel::StorageContext&) {}, [&] { done = true; });
    CHECK_FALSE(done);
    rig.settle();
    CHECK(done);
    CHECK(gateway.pending() == 0);
}

TEST_CASE("rig.settle: a hook runs after the gateway, for a rig that settles itself", "[ui][rig]")
{
    UiRig rig;
    int calls = 0;
    rig.on_settle([&calls] { ++calls; });
    rig.settle();
    rig.settle();
    CHECK(calls == 2);
}

TEST_CASE("rig.theme: the scheme, the text scale and Reduce Motion reach the singleton of the rig's engine",
          "[ui][rig]")
{
    UiRig rig;
    QObject* theme = rig.theme();
    REQUIRE(theme != nullptr);
    CHECK_FALSE(theme->property("dark").toBool());
    rig.set_dark(true);
    CHECK(theme->property("dark").toBool());
    rig.set_text_scale(1.3);
    CHECK(theme->property("textScale").toDouble() == 1.3);
    rig.set_reduce_motion(true);
    CHECK(theme->property("reduceMotion").toBool());
}

// ---- the checks, against a stub that follows the rule and one that breaks it ----

TEST_CASE("a11y.names: found on a button without a name and on two buttons with one name", "[ui][a11y]")
{
    {
        UiRig rig;
        REQUIRE(load_good(rig) != nullptr);
        expect_clean(check_names(rig));
    }
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubBadNames")) != nullptr);
    const Violations found = check_names(rig);
    INFO(describe(found).toStdString());
    CHECK(any_line_has(found, QStringLiteral("no Accessible.name")));
    CHECK(any_line_has(found, QStringLiteral("two controls named \"Add\"")));
    CHECK(any_line_has(found, QStringLiteral("silent")));
}

TEST_CASE("a11y.targets: found on a 30 pt button, passed with a handler margin that makes 44 pt", "[ui][a11y]")
{
    {
        UiRig rig;
        REQUIRE(load_good(rig) != nullptr);
        expect_clean(check_targets(rig));
    }
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubBadTargets")) != nullptr);
    const Violations found = check_targets(rig);
    INFO(describe(found).toStdString());
    CHECK(any_line_has(found, QStringLiteral("small")));
    CHECK_FALSE(any_line_has(found, QStringLiteral("padded")));
    // The tile rule is stricter.
    CHECK(any_line_has(check_targets(rig, [](const Node&) { return 88.0; }), QStringLiteral("padded")));
}

TEST_CASE("a11y.reading_order: found when the lower item is read first", "[ui][a11y]")
{
    {
        UiRig rig;
        REQUIRE(load_good(rig) != nullptr);
        expect_clean(check_reading_order(rig));
    }
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubBadOrder")) != nullptr);
    const Violations found = check_reading_order(rig);
    INFO(describe(found).toStdString());
    CHECK_FALSE(found.empty());
}

TEST_CASE("a11y.text_scale: found when a label does not grow with the text size", "[ui][a11y]")
{
    {
        UiRig rig;
        REQUIRE(load_good(rig) != nullptr);
        expect_clean(check_everything(rig));
    }
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubBadScale")) != nullptr);
    rig.set_text_scale(1.0);
    const bool fits_at_one = check_text_scale(rig).empty();
    rig.set_text_scale(3.1);
    const Violations found = check_text_scale(rig);
    INFO(describe(found).toStdString());
    CHECK(fits_at_one);
    CHECK_FALSE(found.empty());
}

TEST_CASE("a11y.safe_area: found when a button is inside the Dynamic Island, and for a different phone", "[ui][a11y]")
{
    {
        UiRig rig;
        REQUIRE(load_good(rig) != nullptr);
        expect_clean(check_safe_area(rig, 59, 34));
        // A phone with larger insets: the stub's frame is fixed at 59 and 34, so it does not clear 79 and 54.
        CHECK_FALSE(check_safe_area(rig, 79, 54).empty());
    }
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubBadSafeArea")) != nullptr);
    const Violations found = check_safe_area(rig, 59, 34);
    INFO(describe(found).toStdString());
    CHECK(any_line_has(found, QStringLiteral("top inset")));
}

TEST_CASE("motion.reduce: found on a 400 ms slide, passed on one that takes its values from Theme", "[ui][motion]")
{
    {
        UiRig rig;
        rig.set_reduce_motion(true);
        REQUIRE(rig.load(QStringLiteral("UiStubMotion")) != nullptr);
        expect_clean(check_motion_reduced(rig));
    }
    {
        // With Reduce Motion off the same stub does slide, which is how the check knows it can tell the two apart.
        UiRig rig;
        rig.set_reduce_motion(false);
        REQUIRE(rig.load(QStringLiteral("UiStubMotion")) != nullptr);
        CHECK_FALSE(check_motion_reduced(rig).empty());
    }
    UiRig rig;
    rig.set_reduce_motion(true);
    REQUIRE(rig.load(QStringLiteral("UiStubBadMotion")) != nullptr);
    const Violations found = check_motion_reduced(rig);
    INFO(describe(found).toStdString());
    CHECK_FALSE(found.empty());
}
