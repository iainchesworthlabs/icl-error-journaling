// SPDX-License-Identifier: GPL-3.0-or-later
// The perf cases of the rig (docs/plan/stage-4.md, "Tests"): perf.press_path, perf.scene and perf.first_frame, shown on
// the stub that follows the rules. Packages 4.5 and 4.7 point the same helpers at the Tile and at the Log screen.
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQuickItem>
#include <QVariantMap>

using namespace ej::ui;

namespace
{

constexpr double PRESS_BUDGET_MS = 16.0;
constexpr int PRESSES = 200;

QJsonObject budget(const QString& name)
{
    QFile file(QStringLiteral(EJ_PERF_BUDGET));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object().value(QStringLiteral("scene")).toObject().value(name).toObject();
}

} // namespace

TEST_CASE("perf.press_path: from a synthetic press to the handler, p95 over 200 presses is under 16 ms", "[ui][perf]")
{
    UiRig rig;
    QQuickItem* item =
        rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("Sam Example")}});
    REQUIRE(item != nullptr);
    QQuickItem* button = rig.find(QStringLiteral("activate"));
    REQUIRE(button != nullptr);

    int handled = 0;
    int finger = 0;
    // Only the press is timed: from the touch event to the handler having run.
    const std::vector<double> times = time_each(
        PRESSES, [&] { finger = rig.press(button); },
        [&]
        {
            handled = item->property("presses").toInt();
            rig.release(finger);
        });
    CHECK(handled == PRESSES);
    const double p95 = percentile(times, 0.95);
    INFO("p95 " << p95 << " ms, median " << percentile(times, 0.5) << " ms, worst " << times.back() << " ms");
    CHECK(p95 < PRESS_BUDGET_MS * perf_budget_factor());
}

TEST_CASE("perf.press_path: the helpers measure what they say", "[ui]")
{
    CHECK(percentile({}, 0.95) == 0);
    CHECK(percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 0.5) == 5);
    CHECK(percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 0.95) == 10);
    CHECK(percentile({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}, 0.1) == 1);
    int calls = 0;
    const std::vector<double> times = time_each(7, [&calls] { ++calls; });
    CHECK(calls == 7);
    CHECK(times.size() == 7);
    CHECK(std::is_sorted(times.begin(), times.end()));
}

TEST_CASE("perf.scene: the item and delegate counts stay under the budget in perf-budget.json", "[ui][perf]")
{
    UiRig rig;
    REQUIRE(rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("Sam Example")}}) !=
            nullptr);
    const SceneSize size = measure_scene(rig);
    const QJsonObject limit = budget(QStringLiteral("UiStubGood"));
    REQUIRE_FALSE(limit.isEmpty());
    INFO("items " << size.m_Items << ", delegates " << size.m_Delegates);
    CHECK(size.m_Items <= limit.value(QStringLiteral("items")).toInt());
    CHECK(size.m_Delegates <= limit.value(QStringLiteral("delegates")).toInt());
}

TEST_CASE("perf.scene: a Repeater's delegates are counted", "[ui]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        Item {
            Repeater {
                model: 5
                Rectangle { required property int index; required property var model; width: 10; height: 10 }
            }
        })")) != nullptr);
    CHECK(measure_scene(rig).m_Delegates == 5);
}

TEST_CASE("perf.first_frame: changing the text of a label moves no rectangle", "[ui][perf]")
{
    UiRig rig;
    QQuickItem* item =
        rig.load(QStringLiteral("UiStubGood"), {{QStringLiteral("title"), QStringLiteral("Sam Example")}});
    REQUIRE(item != nullptr);
    const std::vector<Geometry> before = snapshot_geometry(rig);
    REQUIRE_FALSE(before.empty());
    // Same size of text, other words: the title is one line either way, so nothing below it moves.
    item->setProperty("buttonLabel", QStringLiteral("Save"));
    rig.layout();
    CHECK(moved_items(before, snapshot_geometry(rig)).empty());

    // A layout that does change is found, so the check can fail.
    item->setProperty(
        "title", QStringLiteral("A title long enough that it has to wrap onto a second line of text in the frame"));
    rig.layout();
    CHECK_FALSE(moved_items(before, snapshot_geometry(rig)).empty());
}
