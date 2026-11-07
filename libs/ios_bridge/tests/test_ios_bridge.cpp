// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/ios_bridge.hpp"
#include "error_journaling/qml_bridge.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>

#include <string>
#include <utility>
#include <vector>

namespace bridge = error_journaling::bridge;

namespace
{

// Records the calls and ends the share sheet with a chosen outcome.
class fake_services final : public bridge::platform_services
{
public:
    bridge::share_result next_result;
    std::vector<bridge::haptic_kind> fired;
    int prepared{0};
    std::string shared_text;

    void present_share_sheet(std::string_view text, bridge::share_callback done) override
    {
        shared_text = std::string(text);
        done(next_result);
    }
    void prepare_haptics() override { ++prepared; }
    void fire_haptic(bridge::haptic_kind kind) override { fired.push_back(kind); }
};

class deferred_services final : public bridge::platform_services
{
public:
    bridge::share_callback held;
    void present_share_sheet(std::string_view, bridge::share_callback done) override { held = std::move(done); }
    void prepare_haptics() override {}
    void fire_haptic(bridge::haptic_kind) override {}
};

// Records every shareFinished: {outcome, activity type}.
struct share_log
{
    QList<std::pair<QString, QString>> m_Entries;
    explicit share_log(bridge::qml_bridge& qml)
    {
        QObject::connect(&qml, &bridge::qml_bridge::shareFinished, &qml, [this](const QString& o, const QString& a) {
            m_Entries.append({o, a});
        });
    }
};

// QCoreApplication wants an argc that outlives it.
struct app_fixture
{
    int m_Argc{0};
    QCoreApplication m_App{m_Argc, nullptr};
};

}  // namespace

TEST_CASE("The bridge reports the implementation CMake linked", "[ios_bridge]")
{
    // The tests run on a desktop, so the stub is the one linked.
    CHECK(bridge::implementation_name() == "desktop-stub");
}

TEST_CASE("The desktop stub reports the share sheet as unavailable", "[ios_bridge]")
{
    const auto services = bridge::make_platform_services();
    int calls = 0;
    bridge::share_result seen{bridge::share_outcome::shared, "x"};
    services->present_share_sheet(bridge::spike_share_text, [&](const bridge::share_result& r) {
        ++calls;
        seen = r;
    });
    CHECK(calls == 1);
    CHECK(seen.outcome == bridge::share_outcome::unavailable);
    CHECK(seen.activity_type.empty());
}

TEST_CASE("The desktop stub accepts haptic calls and does nothing", "[ios_bridge]")
{
    const auto services = bridge::make_platform_services();
    services->prepare_haptics();
    services->fire_haptic(bridge::haptic_kind::impact);
    services->fire_haptic(bridge::haptic_kind::success);
    SUCCEED();
}

TEST_CASE("The spike text is the fictional stand-in", "[ios_bridge]")
{
    CHECK(bridge::spike_share_text.find("Sam Example") != std::string_view::npos);
}

TEST_CASE("QML sees the stub as having no share sheet and gets 'unavailable'", "[ios_bridge][qml]")
{
    app_fixture app;
    bridge::qml_bridge qml(bridge::make_platform_services());
    CHECK_FALSE(qml.share_available());

    share_log spy(qml);
    qml.shareSpikeText();
    REQUIRE(spy.m_Entries.size() == 1);
    CHECK(spy.m_Entries[0].first == "unavailable");
    CHECK(spy.m_Entries[0].second.isEmpty());
}

TEST_CASE("QML maps each share outcome and the activity type to the signal", "[ios_bridge][qml]")
{
    app_fixture app;
    auto owned = std::make_unique<fake_services>();
    auto* fake = owned.get();
    bridge::qml_bridge qml(std::move(owned));
    share_log spy(qml);

    fake->next_result = {bridge::share_outcome::shared, "com.example.activity"};
    qml.shareSpikeText();
    fake->next_result = {bridge::share_outcome::cancelled, {}};
    qml.shareSpikeText();
    fake->next_result = {bridge::share_outcome::failed, {}};
    qml.shareSpikeText();

    REQUIRE(spy.m_Entries.size() == 3);
    CHECK(spy.m_Entries[0].first == "shared");
    CHECK(spy.m_Entries[0].second == "com.example.activity");
    CHECK(spy.m_Entries[1].first == "cancelled");
    CHECK(spy.m_Entries[2].first == "failed");
    CHECK(fake->shared_text == bridge::spike_share_text);
}

TEST_CASE("QML haptic calls reach the services, impact and success apart", "[ios_bridge][qml]")
{
    app_fixture app;
    auto owned = std::make_unique<fake_services>();
    auto* fake = owned.get();
    bridge::qml_bridge qml(std::move(owned));

    qml.prepareHaptics();
    qml.hapticImpact();
    qml.hapticSuccess();

    CHECK(fake->prepared == 1);
    REQUIRE(fake->fired.size() == 2);
    CHECK(fake->fired[0] == bridge::haptic_kind::impact);
    CHECK(fake->fired[1] == bridge::haptic_kind::success);
}

TEST_CASE("A share that ends after the bridge is gone does not signal or crash", "[ios_bridge][qml]")
{
    app_fixture app;
    bridge::share_callback late;
    {
        auto owned = std::make_unique<deferred_services>();
        auto* raw = owned.get();
        bridge::qml_bridge qml(std::move(owned));
        qml.shareSpikeText();
        late = raw->held;
    }
    late({bridge::share_outcome::shared, {}});
    SUCCEED();
}
