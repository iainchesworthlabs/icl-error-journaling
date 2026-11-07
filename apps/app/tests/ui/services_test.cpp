// SPDX-License-Identifier: GPL-3.0-or-later
// The rig over the Rig of Stage 3: a screen gets the real view models, and a test sees the state between "submitted"
// and "committed" and moves the undo window by hand.
#include "rig/services.hpp"
#include "rig/ui_rig.hpp"
#include "support/rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QQuickItem>
#include <chrono>

using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

constexpr auto SCREEN = R"(
    import QtQuick
    Item {
        required property QtObject session
        required property QtObject undo
        required property QtObject board
        required property QtObject picker
        required property QtObject timeline
        required property QtObject history
        required property QtObject day
        required property QtObject settings
        required property QtObject share
        property int total: board.totalCount
        property bool toast: undo.active
        property string toastText: undo.text
    })";

} // namespace

TEST_CASE("services: a screen gets the real view models as required properties", "[ui][services]")
{
    ej::test::Rig services(ej::test::RigStore::MEMORY);
    services.import_sample_configuration();
    UiRig rig;
    use(rig, services);

    QQuickItem* screen = rig.load_source(QByteArray(SCREEN), screen_properties(services));
    INFO(rig.errors().toStdString());
    REQUIRE(screen != nullptr);
    CHECK(screen->property("total").toInt() == 0);
    CHECK_FALSE(screen->property("toast").toBool());
    CHECK(screen->property("board").value<QObject*>() == &services.board());
}

TEST_CASE("services: between a tap and its commit nothing has changed, and settle makes the toast", "[ui][services]")
{
    ej::test::Rig services(ej::test::RigStore::MEMORY);
    services.import_sample_configuration();
    UiRig rig;
    use(rig, services);
    QQuickItem* screen = rig.load_source(QByteArray(SCREEN), screen_properties(services));
    REQUIRE(screen != nullptr);

    const QString mute_delay = type_id(services, QStringLiteral("Mute Delay"));
    REQUIRE_FALSE(mute_delay.isEmpty());
    CHECK(services.tap(mute_delay)); // the haptic, then recordError
    CHECK(board_count(services, QStringLiteral("Mute Delay")) == 0);
    CHECK_FALSE(screen->property("toast").toBool());

    rig.settle(); // the commit, then everything it sets off
    CHECK(board_count(services, QStringLiteral("Mute Delay")) == 1);
    CHECK(screen->property("total").toInt() == 1);
    CHECK(screen->property("toast").toBool());
    CHECK(screen->property("toastText").toString().contains(QStringLiteral("Mute Delay")));
}

TEST_CASE("services: advancing the rig moves the undo window", "[ui][services]")
{
    ej::test::Rig services(ej::test::RigStore::MEMORY);
    services.import_sample_configuration();
    UiRig rig;
    use(rig, services);
    QQuickItem* screen = rig.load_source(QByteArray(SCREEN), screen_properties(services));
    REQUIRE(screen != nullptr);
    CHECK(services.tap(type_id(services, QStringLiteral("Mute Delay"))));
    rig.settle();
    REQUIRE(screen->property("toast").toBool());

    rig.advance(4s);
    CHECK(screen->property("toast").toBool());
    rig.advance(1100ms);
    CHECK_FALSE(screen->property("toast").toBool());
    CHECK(screen->property("total").toInt() == 1); // the entry stays; only the chance to undo it is gone
}

TEST_CASE("services: extra properties are added to the models", "[ui][services]")
{
    ej::test::Rig services(ej::test::RigStore::MEMORY);
    const QVariantMap properties = screen_properties(services, {{QStringLiteral("bridge"), 7}});
    CHECK(properties.value(QStringLiteral("bridge")).toInt() == 7);
    CHECK(properties.size() == 10);
}
