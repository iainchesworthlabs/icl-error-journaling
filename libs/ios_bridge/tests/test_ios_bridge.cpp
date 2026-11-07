// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/accessibility_settings.hpp"
#include "error_journaling/config_exporter.hpp"
#include "error_journaling/config_importer.hpp"
#include "error_journaling/document_picker.hpp"
#include "error_journaling/document_types.hpp"
#include "error_journaling/haptic_policy.hpp"
#include "error_journaling/launch_tracker.hpp"
#include "error_journaling/open_file_filter.hpp"
#include "error_journaling/perf_marks.hpp"
#include "error_journaling/platform_services.hpp"
#include "error_journaling/qml_bridge.hpp"
#include "error_journaling/share_presenter.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <QCoreApplication>
#include <QDir>
#include <QFileOpenEvent>
#include <QList>
#include <QObject>
#include <QString>
#include <QUrl>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace bridge = ej::bridge;
namespace fs = std::filesystem;

namespace
{

// Records the requests and ends each sheet with a chosen outcome, at once or when the test says.
class FakeShare final : public bridge::Share
{
public:
    bridge::ShareResult m_Next{bridge::ShareOutcome::SHARED, "com.example.activity", {}};
    bool m_Hold = false;
    std::vector<bridge::ShareRequest> m_Requests;
    bridge::ShareCallback m_Held;

    [[nodiscard]] bool available() const noexcept override
    {
        return true;
    }

    void present(bridge::ShareRequest request, bridge::ShareCallback done) override
    {
        m_Requests.push_back(std::move(request));
        if (m_Hold)
        {
            m_Held = std::move(done);
            return;
        }
        done(m_Next);
    }

    void complete(const bridge::ShareResult& result)
    {
        auto done = std::move(m_Held);
        m_Held = nullptr;
        done(result);
    }
};

class FakeHaptics final : public bridge::Haptics
{
public:
    std::vector<bridge::HapticKind> m_Fired;
    int m_Prepared = 0;

    void prepare() override
    {
        ++m_Prepared;
    }
    void fire(bridge::HapticKind kind) override
    {
        m_Fired.push_back(kind);
    }
};

// Records the marks in order, as text, against a clock the test moves.
class FakePerfMarks final : public bridge::PerfMarks
{
public:
    std::vector<std::string> m_Calls;
    std::vector<std::string> m_Lines;
    double m_SinceStart = 0;
    double m_Uptime = 100000;

    void begin(bridge::Mark mark) override
    {
        m_Calls.push_back("begin " + name(mark));
    }
    void end(bridge::Mark mark) override
    {
        m_Calls.push_back("end " + name(mark));
    }
    void event(bridge::Mark mark, double ms) override
    {
        m_Calls.push_back("event " + name(mark) + " " + std::to_string(static_cast<long long>(ms)));
    }
    double ms_since_process_start() override
    {
        return m_SinceStart;
    }
    double uptime_ms() override
    {
        return m_Uptime;
    }
    void log(std::string_view line) override
    {
        m_Lines.emplace_back(line);
    }

    static std::string name(bridge::Mark mark)
    {
        switch (mark)
        {
        case bridge::Mark::PROCESS_START:
            return "process";
        case bridge::Mark::STORAGE_READY:
            return "storage";
        case bridge::Mark::QML_LOADED:
            return "qml";
        case bridge::Mark::FIRST_FRAME:
            return "frame";
        case bridge::Mark::BOARD_USABLE:
            return "usable";
        case bridge::Mark::TAP_TO_HAPTIC:
            return "tap_to_haptic";
        case bridge::Mark::TOUCH_AGE:
            return "touch_age";
        case bridge::Mark::TAP_TO_COMMIT:
            return "tap_to_commit";
        }
        return "?";
    }
};

// Settings the test changes, and a handler it can call as the system would.
class FakeAccessibility final : public bridge::AccessibilitySettings
{
public:
    bridge::AccessibilityState m_State;
    std::function<void()> m_Handler;

    [[nodiscard]] bridge::AccessibilityState read() const override
    {
        return m_State;
    }
    void set_change_handler(std::function<void()> changed) override
    {
        m_Handler = std::move(changed);
    }
    void notify() const
    {
        if (m_Handler)
        {
            m_Handler();
        }
    }
};

struct Fakes
{
    FakeAccessibility* m_Accessibility = nullptr;
    FakeShare* m_Share = nullptr;
    FakeHaptics* m_Haptics = nullptr;
    FakePerfMarks* m_Marks = nullptr;
};

bridge::PlatformServices fake_services(Fakes& fakes)
{
    bridge::PlatformServices services;
    auto share = std::make_unique<FakeShare>();
    auto haptics = std::make_unique<FakeHaptics>();
    fakes.m_Share = share.get();
    fakes.m_Haptics = haptics.get();
    auto marks = std::make_unique<FakePerfMarks>();
    fakes.m_Marks = marks.get();
    services.m_PerfMarks = std::move(marks);
    auto accessibility = std::make_unique<FakeAccessibility>();
    fakes.m_Accessibility = accessibility.get();
    services.m_Accessibility = std::move(accessibility);
    services.m_Share = std::move(share);
    services.m_Haptics = std::move(haptics);
    return services;
}

// Records every shareFinished: {outcome, activity type}.
struct ShareLog
{
    QList<std::pair<QString, QString>> m_Entries;
    explicit ShareLog(bridge::QmlBridge& qml)
    {
        QObject::connect(&qml, &bridge::QmlBridge::shareFinished, &qml,
                         [this](const QString& o, const QString& a) { m_Entries.append({o, a}); });
    }
};

// QCoreApplication wants an argc that outlives it.
struct AppFixture
{
    int m_Argc{0};
    QCoreApplication m_App{m_Argc, nullptr};

    void drain()
    {
        QCoreApplication::sendPostedEvents(nullptr, 0);
    }
};

// A fresh directory under the working directory (the build tree), removed afterwards.
class TempDirectory
{
public:
    TempDirectory()
    {
        static std::atomic<unsigned> counter{0};
        m_Path = fs::current_path() / "ej-bridge-test-tmp" / std::to_string(++counter);
        fs::remove_all(m_Path);
        fs::create_directories(m_Path);
    }
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    ~TempDirectory()
    {
        std::error_code ignored;
        fs::remove_all(m_Path, ignored);
    }
    [[nodiscard]] const fs::path& path() const
    {
        return m_Path;
    }

private:
    fs::path m_Path;
};

void write_file(const fs::path& path, const std::string& content)
{
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string read_file(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

TEST_CASE("share.stub: the desktop stub reports UNAVAILABLE and calls back once", "[unit][share]")
{
    const auto share = bridge::make_share();
    CHECK_FALSE(share->available());
    int calls = 0;
    bridge::ShareResult seen{bridge::ShareOutcome::SHARED, "x", "y"};
    bridge::ShareRequest request;
    request.m_Text = "Sam Example logged a Wrong Response on UNIT at North Site.";
    share->present(std::move(request),
                   [&](const bridge::ShareResult& result)
                   {
                       ++calls;
                       seen = result;
                   });
    CHECK(calls == 1);
    CHECK(seen.m_Outcome == bridge::ShareOutcome::UNAVAILABLE);
    CHECK(seen.m_ActivityType.empty());
}

TEST_CASE("haptics.stub: the desktop stub does nothing for either kind", "[unit][haptics]")
{
    const auto haptics = bridge::make_haptics();
    haptics->prepare();
    haptics->fire(bridge::HapticKind::IMPACT);
    haptics->fire(bridge::HapticKind::SUCCESS);
    SUCCEED();
}

TEST_CASE("The factory makes every capability", "[unit][share]")
{
    const bridge::PlatformServices services = bridge::make_platform_services();
    CHECK(services.m_Share != nullptr);
    CHECK(services.m_Haptics != nullptr);
}

TEST_CASE("share.wrapper: the stub gives 'unavailable' on a queued call", "[unit][share]")
{
    AppFixture app;
    bridge::QmlBridge qml(bridge::make_platform_services());
    CHECK_FALSE(qml.share_available());
    ShareLog spy(qml);

    qml.shareText(QStringLiteral("x"));
    CHECK(spy.m_Entries.isEmpty()); // queued: nothing before the event loop turns
    app.drain();
    REQUIRE(spy.m_Entries.size() == 1);
    CHECK(spy.m_Entries[0].first == "unavailable");
    CHECK(spy.m_Entries[0].second.isEmpty());
    CHECK_FALSE(qml.sharing());
}

TEST_CASE("share.wrapper: each outcome maps to its QML string and the activity type passes through", "[unit][share]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    ShareLog spy(qml);

    fakes.m_Share->m_Next = {bridge::ShareOutcome::SHARED, "com.example.activity", {}};
    qml.shareText(QStringLiteral("a"));
    fakes.m_Share->m_Next = {bridge::ShareOutcome::CANCELLED, {}, {}};
    qml.shareText(QStringLiteral("b"));
    fakes.m_Share->m_Next = {bridge::ShareOutcome::UNAVAILABLE, {}, {}};
    qml.shareText(QStringLiteral("c"));
    fakes.m_Share->m_Next = {bridge::ShareOutcome::FAILED, {}, {}};
    qml.shareText(QStringLiteral("d"));
    app.drain();

    REQUIRE(spy.m_Entries.size() == 4);
    CHECK(spy.m_Entries[0].first == "shared");
    CHECK(spy.m_Entries[0].second == "com.example.activity");
    CHECK(spy.m_Entries[1].first == "cancelled");
    CHECK(spy.m_Entries[2].first == "unavailable");
    CHECK(spy.m_Entries[3].first == "failed");
}

TEST_CASE("share.wrapper: a deferred completion is delivered queued and clears sharing", "[unit][share]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    fakes.m_Share->m_Hold = true;
    ShareLog spy(qml);
    int changes = 0;
    QObject::connect(&qml, &bridge::QmlBridge::sharingChanged, &qml, [&] { ++changes; });

    qml.shareText(QStringLiteral("x"));
    CHECK(qml.sharing());
    CHECK(changes == 1);
    fakes.m_Share->complete({bridge::ShareOutcome::SHARED, "a", {}});
    CHECK(spy.m_Entries.isEmpty());
    app.drain();
    REQUIRE(spy.m_Entries.size() == 1);
    CHECK_FALSE(qml.sharing());
    CHECK(changes == 2);
}

TEST_CASE("share.wrapper: a completion after the bridge is gone does nothing", "[unit][share]")
{
    AppFixture app;
    bridge::ShareCallback late;
    {
        Fakes fakes;
        bridge::QmlBridge qml(fake_services(fakes));
        fakes.m_Share->m_Hold = true;
        qml.shareText(QStringLiteral("x"));
        late = fakes.m_Share->m_Held;
    }
    late({bridge::ShareOutcome::SHARED, {}, {}});
    app.drain();
    SUCCEED();
}

TEST_CASE("share.single_flight: a second request is FAILED and never reaches the share", "[unit][share]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    fakes.m_Share->m_Hold = true;
    ShareLog spy(qml);

    qml.shareText(QStringLiteral("first"));
    qml.shareText(QStringLiteral("second"));
    CHECK(fakes.m_Share->m_Requests.size() == 1);
    app.drain();
    REQUIRE(spy.m_Entries.size() == 1);
    CHECK(spy.m_Entries[0].first == "failed");
    CHECK(qml.sharing()); // the first sheet is still up

    fakes.m_Share->complete({bridge::ShareOutcome::CANCELLED, {}, {}});
    app.drain();
    REQUIRE(spy.m_Entries.size() == 2);
    CHECK(spy.m_Entries[1].first == "cancelled");
    CHECK_FALSE(qml.sharing());

    // The flag clears after a completion, and after a FAILED one.
    fakes.m_Share->m_Hold = false;
    fakes.m_Share->m_Next = {bridge::ShareOutcome::FAILED, {}, {}};
    qml.shareText(QStringLiteral("third"));
    qml.shareText(QStringLiteral("fourth"));
    CHECK(fakes.m_Share->m_Requests.size() == 3);
}

TEST_CASE("share.bytes: the PRD example reaches the share byte for byte", "[unit][share]")
{
    const std::string expected = read_file(fs::path(EJ_GOLDEN_DIR) / "prd-example.txt");
    REQUIRE_FALSE(expected.empty());
    CHECK(expected.find("\xE2\x80\x93") != std::string::npos); // the en dash
    CHECK(expected.find("\xE2\x86\x92") != std::string::npos); // the arrow

    FakeShare share;
    bridge::SharePresenter presenter(share);
    bridge::ShareRequest request;
    request.m_Text = expected;
    presenter.present(std::move(request), [](const bridge::ShareResult&) {});
    REQUIRE(share.m_Requests.size() == 1);
    CHECK(share.m_Requests[0].m_Text == expected);
}

TEST_CASE("share.bytes: through QML the same text arrives as UTF-8", "[unit][share]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    const QString text = QString::fromUtf8("T|a \xE2\x80\x93 b \xE2\x86\x92 c\n");
    qml.shareText(text);
    REQUIRE(fakes.m_Share->m_Requests.size() == 1);
    CHECK(fakes.m_Share->m_Requests[0].m_Text == text.toStdString());
}

TEST_CASE("share.bytes: invalid UTF-8, both members or neither give FAILED and no call", "[unit][share]")
{
    FakeShare share;
    bridge::SharePresenter presenter(share);
    std::vector<bridge::ShareOutcome> outcomes;
    const auto note = [&](const bridge::ShareResult& result) { outcomes.push_back(result.m_Outcome); };

    bridge::ShareRequest lone;
    lone.m_Text = "ok \xFF";
    presenter.present(lone, note);

    bridge::ShareRequest truncated;
    truncated.m_Text = "caf\xC3";
    presenter.present(truncated, note);

    bridge::ShareRequest both;
    both.m_Text = "text";
    both.m_File = "a.ejconfig";
    presenter.present(both, note);

    presenter.present(bridge::ShareRequest{}, note);

    CHECK(share.m_Requests.empty());
    REQUIRE(outcomes.size() == 4);
    for (const auto outcome : outcomes)
    {
        CHECK(outcome == bridge::ShareOutcome::FAILED);
    }
    CHECK_FALSE(presenter.in_flight());
}

TEST_CASE("is_valid_utf8 tells good bytes from bad", "[unit][share]")
{
    CHECK(bridge::is_valid_utf8(""));
    CHECK(bridge::is_valid_utf8("plain"));
    CHECK(bridge::is_valid_utf8("\xC3\xA9 \xE2\x80\x93 \xF0\x9F\x98\x80"));
    CHECK_FALSE(bridge::is_valid_utf8("\x80"));             // a lone continuation byte
    CHECK_FALSE(bridge::is_valid_utf8("\xC0\xAF"));         // overlong
    CHECK_FALSE(bridge::is_valid_utf8("\xED\xA0\x80"));     // a surrogate
    CHECK_FALSE(bridge::is_valid_utf8("\xF4\x90\x80\x80")); // above U+10FFFF
    CHECK_FALSE(bridge::is_valid_utf8("\xE2\x80"));         // truncated
    CHECK_FALSE(bridge::is_valid_utf8("\xE2\x28\xA1"));     // a bad continuation
}

TEST_CASE("haptics.wrapper: the QML calls reach the haptics, impact and success apart", "[unit][haptics]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));

    qml.prepareHaptics();
    qml.hapticImpact();
    qml.hapticImpact(123456.0);
    qml.hapticSuccess();

    // The wrapper prepares only when asked: a fire is a fire and nothing else (the Haptics implementation prepares).
    CHECK(fakes.m_Haptics->m_Prepared == 1);
    REQUIRE(fakes.m_Haptics->m_Fired.size() == 3);
    CHECK(fakes.m_Haptics->m_Fired[0] == bridge::HapticKind::IMPACT);
    CHECK(fakes.m_Haptics->m_Fired[1] == bridge::HapticKind::IMPACT);
    CHECK(fakes.m_Haptics->m_Fired[2] == bridge::HapticKind::SUCCESS);
}

TEST_CASE("share.config_file: a shared file is gone after every outcome", "[integration][share]")
{
    AppFixture app;
    TempDirectory directory;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    ShareLog spy(qml);

    const auto outcome =
        GENERATE(bridge::ShareOutcome::SHARED, bridge::ShareOutcome::CANCELLED, bridge::ShareOutcome::FAILED);
    const fs::path file = directory.path() / "Error Journaling setup.ejconfig";
    write_file(file, "{\"schema_version\":1}");
    fakes.m_Share->m_Hold = true;
    fakes.m_Share->m_Next = {outcome, {}, {}};

    qml.shareTemporaryFile(QString::fromStdU16String(file.u16string()));
    REQUIRE(fakes.m_Share->m_Requests.size() == 1);
    CHECK(fakes.m_Share->m_Requests[0].m_File == file);
    CHECK(fs::exists(file)); // still there while the sheet is up

    fakes.m_Share->complete(fakes.m_Share->m_Next);
    CHECK_FALSE(fs::exists(file));
    app.drain();
    CHECK(spy.m_Entries.size() == 1);
}

TEST_CASE("share.config_file: a refused request still removes its file", "[integration][share]")
{
    AppFixture app;
    TempDirectory directory;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    fakes.m_Share->m_Hold = true;

    qml.shareText(QStringLiteral("first"));
    const fs::path file = directory.path() / "second.ejconfig";
    write_file(file, "{}");
    qml.shareTemporaryFile(QString::fromStdU16String(file.u16string()));

    CHECK(fakes.m_Share->m_Requests.size() == 1);
    CHECK_FALSE(fs::exists(file));
}

TEST_CASE("share.config_file: the launch purge removes stale files and nothing else", "[integration][share]")
{
    TempDirectory directory;
    write_file(directory.path() / "stale-one.ejconfig", "{}");
    write_file(directory.path() / "stale-two.ejconfig", "{}");
    fs::create_directories(directory.path() / "kept");
    write_file(directory.path() / "kept" / "inner.txt", "x");

    CHECK(bridge::purge_directory(directory.path()) == 2);
    CHECK_FALSE(fs::exists(directory.path() / "stale-one.ejconfig"));
    CHECK(fs::exists(directory.path() / "kept" / "inner.txt"));
    CHECK(bridge::purge_directory(directory.path() / "missing") == 0);
}

TEST_CASE("haptics.wrapper: the policy calls reach the haptics through QML", "[unit][haptics]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));

    qml.setBoardVisible(true);
    CHECK(fakes.m_Haptics->m_Prepared == 1);
    qml.setApplicationActive(false);
    qml.setApplicationActive(true);
    CHECK(fakes.m_Haptics->m_Prepared == 2);
    CHECK(fakes.m_Haptics->m_Fired.empty());
}

namespace
{

// A policy over a fake haptics and a clock the test moves.
struct PolicyRig
{
    FakeHaptics m_Haptics;
    std::chrono::milliseconds m_Now{1000};
    bridge::HapticPolicy m_Policy;

    explicit PolicyRig(bool keep_alive)
        : m_Policy(
              m_Haptics, [this] { return m_Now; },
              bridge::HapticPolicyConfig{keep_alive, std::chrono::milliseconds{4000}})
    {
    }
};

} // namespace

TEST_CASE("haptics.policy: it prepares when the board becomes visible and when the application becomes active",
          "[unit][haptics]")
{
    PolicyRig rig(false);

    rig.m_Policy.set_board_visible(true);
    CHECK(rig.m_Haptics.m_Prepared == 1);
    rig.m_Policy.set_board_visible(true); // no change, no prepare
    CHECK(rig.m_Haptics.m_Prepared == 1);

    rig.m_Policy.set_application_active(false);
    CHECK(rig.m_Haptics.m_Prepared == 1);
    rig.m_Policy.set_application_active(true);
    CHECK(rig.m_Haptics.m_Prepared == 2);
    rig.m_Policy.set_application_active(true);
    CHECK(rig.m_Haptics.m_Prepared == 2);
}

TEST_CASE("haptics.policy: it prepares on none of them while inactive or hidden", "[unit][haptics]")
{
    PolicyRig rig(true);

    // Hidden board: becoming active and ticking do nothing.
    rig.m_Policy.set_application_active(false);
    rig.m_Policy.set_application_active(true);
    rig.m_Now += std::chrono::seconds(10);
    rig.m_Policy.tick();
    CHECK(rig.m_Haptics.m_Prepared == 0);

    // Inactive application: the board becoming visible and ticking do nothing.
    rig.m_Policy.set_application_active(false);
    rig.m_Policy.set_board_visible(true);
    rig.m_Now += std::chrono::seconds(10);
    rig.m_Policy.tick();
    CHECK(rig.m_Haptics.m_Prepared == 0);

    // Active again, with the board still visible: one prepare, then the board is hidden and a tick does nothing.
    rig.m_Policy.set_application_active(true);
    CHECK(rig.m_Haptics.m_Prepared == 1);
    rig.m_Policy.set_board_visible(false);
    rig.m_Now += std::chrono::seconds(10);
    rig.m_Policy.tick();
    CHECK(rig.m_Haptics.m_Prepared == 1);
}

TEST_CASE("haptics.policy: the keep-alive ticks once per interval, and only when enabled", "[unit][haptics]")
{
    PolicyRig on(true);
    on.m_Policy.set_board_visible(true);
    REQUIRE(on.m_Haptics.m_Prepared == 1);
    on.m_Now += std::chrono::milliseconds(3999);
    on.m_Policy.tick();
    CHECK(on.m_Haptics.m_Prepared == 1);
    on.m_Now += std::chrono::milliseconds(1);
    on.m_Policy.tick();
    CHECK(on.m_Haptics.m_Prepared == 2);
    on.m_Policy.tick(); // the interval starts again from the prepare
    CHECK(on.m_Haptics.m_Prepared == 2);

    PolicyRig off(false);
    off.m_Policy.set_board_visible(true);
    off.m_Now += std::chrono::hours(1);
    off.m_Policy.tick();
    CHECK(off.m_Haptics.m_Prepared == 1);
}

TEST_CASE("haptics.policy: the keep-alive is off by default", "[unit][haptics]")
{
    CHECK_FALSE(bridge::HAPTIC_KEEPALIVE);
    CHECK_FALSE(bridge::HapticPolicyConfig{}.m_KeepAlive);
}

TEST_CASE("perf_marks.stub: the desktop version never throws and time does not go back", "[unit][perf]")
{
    const auto marks = bridge::make_perf_marks();
    marks->begin(bridge::Mark::TAP_TO_HAPTIC);
    marks->end(bridge::Mark::TAP_TO_HAPTIC);
    marks->event(bridge::Mark::BOARD_USABLE, 1.5);
    marks->log("launch: usable at 1 ms");
    const double first = marks->ms_since_process_start();
    const double second = marks->ms_since_process_start();
    CHECK(first >= 0);
    CHECK(second >= first);
    CHECK(marks->uptime_ms() > 0);
}

TEST_CASE("launch_tracker.order: the board is usable once, after storage, QML, the board and a frame", "[unit][perf]")
{
    FakePerfMarks marks;
    bridge::LaunchTracker tracker(marks);

    marks.m_SinceStart = 20;
    tracker.process_started();
    marks.m_SinceStart = 140;
    tracker.storage_ready();
    marks.m_SinceStart = 380;
    tracker.qml_loaded();

    // A frame before the board is enabled is the first frame and nothing more.
    marks.m_SinceStart = 610;
    tracker.frame_swapped();
    CHECK_FALSE(tracker.usable());
    CHECK(marks.m_Lines.empty());

    marks.m_SinceStart = 700;
    tracker.board_enabled();
    CHECK_FALSE(tracker.usable()); // the next frame, not this call

    marks.m_SinceStart = 812;
    tracker.frame_swapped();
    CHECK(tracker.usable());
    marks.m_SinceStart = 900;
    tracker.frame_swapped();
    tracker.frame_swapped();

    const std::vector<std::string> expected{"event process 20", "event storage 140", "event qml 380", "event frame 610",
                                            "event usable 812"};
    CHECK(marks.m_Calls == expected);
    REQUIRE(marks.m_Lines.size() == 1);
    CHECK(marks.m_Lines[0] == "launch: usable at 812 ms (storage 140, qml 380, first frame 610)");
}

TEST_CASE("launch_tracker.order: no frame is usable before storage is ready, and repeats do nothing", "[unit][perf]")
{
    FakePerfMarks marks;
    bridge::LaunchTracker tracker(marks);
    tracker.qml_loaded();
    tracker.board_enabled();
    tracker.frame_swapped();
    CHECK_FALSE(tracker.usable());

    marks.m_SinceStart = 50;
    tracker.storage_ready();
    tracker.storage_ready(); // once
    CHECK_FALSE(tracker.usable());
    tracker.frame_swapped();
    CHECK(tracker.usable());
    CHECK(marks.m_Lines.size() == 1);
}

TEST_CASE("perf_marks.wrapper: the impact is bracketed by the interval and carries the touch age", "[unit][perf]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    fakes.m_Marks->m_Uptime = 5000;

    qml.hapticImpact(4955.0);
    const std::vector<std::string> expected{"event touch_age 45", "begin tap_to_haptic", "end tap_to_haptic"};
    CHECK(fakes.m_Marks->m_Calls == expected);
    CHECK(fakes.m_Haptics->m_Fired.size() == 1);
}

TEST_CASE("perf_marks.wrapper: no timestamp, or one on another time base, gives no touch age", "[unit][perf]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    fakes.m_Marks->m_Uptime = 5000;
    const std::vector<std::string> plain{"begin tap_to_haptic", "end tap_to_haptic"};

    qml.hapticImpact();
    CHECK(fakes.m_Marks->m_Calls == plain);

    fakes.m_Marks->m_Calls.clear();
    qml.hapticImpact(9'000'000.0); // in the future of the uptime clock
    CHECK(fakes.m_Marks->m_Calls == plain);

    fakes.m_Marks->m_Calls.clear();
    fakes.m_Marks->m_Uptime = 5'000'000.0;
    qml.hapticImpact(100.0); // hours old: another time base
    CHECK(fakes.m_Marks->m_Calls == plain);
}

TEST_CASE("accessibility.table: every category has a scale, rising with each step", "[unit][accessibility]")
{
    const std::vector<std::string> categories{
        "UICTContentSizeCategoryXS",
        "UICTContentSizeCategoryS",
        "UICTContentSizeCategoryM",
        "UICTContentSizeCategoryL",
        "UICTContentSizeCategoryXL",
        "UICTContentSizeCategoryXXL",
        "UICTContentSizeCategoryXXXL",
        "UICTContentSizeCategoryAccessibilityM",
        "UICTContentSizeCategoryAccessibilityL",
        "UICTContentSizeCategoryAccessibilityXL",
        "UICTContentSizeCategoryAccessibilityXXL",
        "UICTContentSizeCategoryAccessibilityXXXL",
    };
    double previous = 0;
    for (const std::string& category : categories)
    {
        const double scale = bridge::text_scale_for_category(category);
        INFO(category);
        CHECK(scale > previous);
        previous = scale;
    }
    CHECK(bridge::text_scale_for_category("UICTContentSizeCategoryL") == 1.0);
    CHECK(bridge::text_scale_for_category("UICTContentSizeCategoryXS") == Catch::Approx(0.82).margin(0.01));
    CHECK(bridge::text_scale_for_category("UICTContentSizeCategoryAccessibilityXXXL") ==
          Catch::Approx(3.1).margin(0.05));
    // The accessibility sizes are all above 1.6, and the standard sizes all below it.
    CHECK(bridge::text_scale_for_category("UICTContentSizeCategoryXXXL") < 1.4);
    CHECK(bridge::text_scale_for_category("UICTContentSizeCategoryAccessibilityM") > 1.6);
}

TEST_CASE("accessibility.table: an unknown or empty name is the default size", "[unit][accessibility]")
{
    CHECK(bridge::text_scale_for_category("") == 1.0);
    CHECK(bridge::text_scale_for_category("UICTContentSizeCategoryUnspecified") == 1.0);
    CHECK(bridge::text_scale_for_category("nonsense") == 1.0);
}

TEST_CASE("accessibility.stub: the desktop stub is the default size with nothing on and never changes",
          "[unit][accessibility]")
{
    const auto settings = bridge::make_accessibility_settings();
    const bridge::AccessibilityState state = settings->read();
    CHECK(state.m_TextScale == 1.0);
    CHECK_FALSE(state.m_ReduceMotion);
    CHECK_FALSE(state.m_VoiceOverRunning);
    settings->set_change_handler([] {});
    settings->set_change_handler({});
    CHECK(settings->read() == state);
}

TEST_CASE("accessibility.wrapper: QML sees the settings at start and the stub's defaults", "[unit][accessibility]")
{
    AppFixture app;
    bridge::QmlBridge stub(bridge::make_platform_services());
    CHECK(stub.text_scale() == 1.0);
    CHECK_FALSE(stub.reduce_motion());

    Fakes fakes;
    auto services = fake_services(fakes);
    fakes.m_Accessibility->m_State = {1.65, true, true};
    bridge::QmlBridge qml(std::move(services));
    // The fake was given its state after the services were made but before the bridge read them.
    CHECK(qml.text_scale() == Catch::Approx(1.65));
    CHECK(qml.reduce_motion());
    CHECK(qml.voice_over_running());
}

TEST_CASE("accessibility.wrapper: one signal for each setting that changed, none for the same value",
          "[unit][accessibility]")
{
    AppFixture app;
    Fakes fakes;
    bridge::QmlBridge qml(fake_services(fakes));
    int scale = 0;
    int motion = 0;
    int voice = 0;
    QObject::connect(&qml, &bridge::QmlBridge::textScaleChanged, &qml, [&] { ++scale; });
    QObject::connect(&qml, &bridge::QmlBridge::reduceMotionChanged, &qml, [&] { ++motion; });
    QObject::connect(&qml, &bridge::QmlBridge::voiceOverRunningChanged, &qml, [&] { ++voice; });

    fakes.m_Accessibility->notify(); // nothing changed
    CHECK(scale + motion + voice == 0);

    fakes.m_Accessibility->m_State.m_TextScale = 2.35;
    fakes.m_Accessibility->notify();
    CHECK(scale == 1);
    CHECK(motion == 0);
    CHECK(qml.text_scale() == Catch::Approx(2.35));

    fakes.m_Accessibility->notify(); // the same value again
    CHECK(scale == 1);

    fakes.m_Accessibility->m_State.m_ReduceMotion = true;
    fakes.m_Accessibility->m_State.m_VoiceOverRunning = true;
    fakes.m_Accessibility->notify();
    CHECK(scale == 1);
    CHECK(motion == 1);
    CHECK(voice == 1);
    CHECK(qml.reduce_motion());

    fakes.m_Accessibility->m_State.m_ReduceMotion = false;
    fakes.m_Accessibility->notify();
    CHECK(motion == 2);
    CHECK_FALSE(qml.reduce_motion());
}

TEST_CASE("accessibility.wrapper: a notice after the bridge is gone is ignored", "[unit][accessibility]")
{
    AppFixture app;
    std::function<void()> late;
    {
        Fakes fakes;
        bridge::QmlBridge qml(fake_services(fakes));
        late = fakes.m_Accessibility->m_Handler;
    }
    REQUIRE(late);
    late();
    SUCCEED();
}

namespace
{

class FakePicker final : public bridge::DocumentPicker
{
public:
    bridge::PickResult m_Result;
    int m_Calls = 0;

    void pick_configuration(std::function<void(const bridge::PickResult&)> done) override
    {
        ++m_Calls;
        done(m_Result);
    }
};

QString qt_path(const fs::path& path)
{
    return QString::fromStdU16String(path.u16string());
}

std::string sample_config_bytes()
{
    return read_file(fs::path(EJ_SAMPLE_CONFIG));
}

void post_file_open(QObject& target, const QUrl& url)
{
    QCoreApplication::postEvent(&target, new QFileOpenEvent(url));
    QCoreApplication::sendPostedEvents(&target, 0);
}

} // namespace

TEST_CASE("picker.stub: the stub reports UNAVAILABLE once", "[unit][import]")
{
    const auto picker = bridge::make_document_picker();
    int calls = 0;
    bridge::PickOutcome seen = bridge::PickOutcome::PICKED;
    picker->pick_configuration(
        [&](const bridge::PickResult& result)
        {
            ++calls;
            seen = result.m_Outcome;
        });
    CHECK(calls == 1);
    CHECK(seen == bridge::PickOutcome::UNAVAILABLE);
}

TEST_CASE("document_types: the constants name the type and the file", "[unit][import]")
{
    CHECK(bridge::CONFIG_TYPE_IDENTIFIER == "com.iainchesworthlabs.errorjournaling.config");
    CHECK(bridge::CONFIG_EXTENSION == "ejconfig");
    CHECK(bridge::CONFIG_EXPORT_FILE_NAME.ends_with(".ejconfig"));
    CHECK(bridge::CONFIG_MAX_BYTES == 1024 * 1024);
}

TEST_CASE("open_file.queue: a file event before ready is delivered once, after it", "[unit][import]")
{
    AppFixture app;
    TempDirectory directory;
    const fs::path first = directory.path() / "first.ejconfig";
    const fs::path second = directory.path() / "second.json";
    write_file(first, "{}");
    write_file(second, "{}");

    std::vector<std::string> log;
    bridge::OpenFileFilter filter([&](std::string_view line) { log.emplace_back(line); });
    QCoreApplication::instance()->installEventFilter(&filter);
    QStringList opened;
    QObject::connect(&filter, &bridge::OpenFileFilter::fileOpened, &filter,
                     [&](const QString& p) { opened.append(p); });

    post_file_open(*QCoreApplication::instance(), QUrl::fromLocalFile(qt_path(first)));
    CHECK(opened.isEmpty());
    CHECK(filter.pending() == 1);
    CHECK(log.size() == 1);

    filter.ready();
    REQUIRE(opened.size() == 1);
    CHECK(QDir::cleanPath(opened[0]) == QDir::cleanPath(qt_path(first)));

    // A second file waits until the first is handled.
    post_file_open(*QCoreApplication::instance(), QUrl::fromLocalFile(qt_path(second)));
    CHECK(opened.size() == 1);
    filter.handled();
    REQUIRE(opened.size() == 2);
    CHECK(QDir::cleanPath(opened[1]) == QDir::cleanPath(qt_path(second)));

    filter.handled();
    CHECK(opened.size() == 2);
    QCoreApplication::instance()->removeEventFilter(&filter);
}

TEST_CASE("open_file.queue: a URL that is not a local file, an empty URL and a directory are ignored", "[unit][import]")
{
    AppFixture app;
    TempDirectory directory;
    bridge::OpenFileFilter filter;
    QCoreApplication::instance()->installEventFilter(&filter);
    filter.ready();
    int opened = 0;
    QObject::connect(&filter, &bridge::OpenFileFilter::fileOpened, &filter, [&](const QString&) { ++opened; });

    post_file_open(*QCoreApplication::instance(), QUrl(QStringLiteral("https://example.invalid/setup.ejconfig")));
    post_file_open(*QCoreApplication::instance(), QUrl());
    post_file_open(*QCoreApplication::instance(), QUrl::fromLocalFile(qt_path(directory.path())));
    CHECK(opened == 0);
    CHECK(filter.pending() == 0);
    QCoreApplication::instance()->removeEventFilter(&filter);
}

TEST_CASE("importer.sample: the sample goes through as a .ejconfig and as a .json, byte for byte, and is deleted",
          "[integration][import]")
{
    AppFixture app;
    TempDirectory directory;
    const std::string sample = sample_config_bytes();
    REQUIRE_FALSE(sample.empty());

    for (const char* name : {"setup.ejconfig", "setup.json"})
    {
        const fs::path file = directory.path() / name;
        write_file(file, sample);
        std::string seen;
        int loads = 0;
        bridge::ConfigImporter importer(
            [&](const QString& path)
            {
                ++loads;
                seen = read_file(fs::path(path.toStdU16String()));
            });
        QStringList failures;
        QObject::connect(&importer, &bridge::ConfigImporter::failed, &importer,
                         [&](const QStringList& m) { failures += m; });

        importer.import_file(qt_path(file));
        CHECK(loads == 1);
        CHECK(seen == sample);
        CHECK(importer.busy());
        CHECK(fs::exists(file)); // until the settings model has read it

        importer.load_finished();
        CHECK_FALSE(fs::exists(file));
        CHECK_FALSE(importer.busy());
        CHECK(failures.isEmpty());
    }
}

TEST_CASE(
    "importer.errors: an oversize file is refused unread; a missing file and a directory are refused; all are gone",
    "[integration][import]")
{
    AppFixture app;
    TempDirectory directory;
    int loads = 0;
    bridge::ConfigImporter importer([&](const QString&) { ++loads; });
    QStringList failures;
    QObject::connect(&importer, &bridge::ConfigImporter::failed, &importer,
                     [&](const QStringList& m) { failures += m; });

    const fs::path big = directory.path() / "big.ejconfig";
    write_file(big, std::string(bridge::CONFIG_MAX_BYTES + 1, ' '));
    importer.import_file(qt_path(big));
    CHECK(loads == 0);
    CHECK(failures.size() == 1);
    CHECK_FALSE(fs::exists(big));

    importer.import_file(qt_path(directory.path() / "missing.ejconfig"));
    importer.import_file(qt_path(directory.path()));
    CHECK(loads == 0);
    CHECK(failures.size() == 3);

    // A file of exactly the limit is handed on.
    const fs::path exact = directory.path() / "exact.ejconfig";
    write_file(exact, std::string(bridge::CONFIG_MAX_BYTES, ' '));
    importer.import_file(qt_path(exact));
    CHECK(loads == 1);
    importer.load_finished();
    CHECK_FALSE(fs::exists(exact));
}

TEST_CASE("importer.errors: a file the settings refuse is deleted all the same, and the next one goes on",
          "[integration][import]")
{
    AppFixture app;
    TempDirectory directory;
    std::vector<std::string> loaded;
    bridge::ConfigImporter importer([&](const QString& path)
                                    { loaded.push_back(read_file(fs::path(path.toStdU16String()))); });
    int idles = 0;
    QObject::connect(&importer, &bridge::ConfigImporter::idle, &importer, [&] { ++idles; });

    const fs::path malformed = directory.path() / "malformed.ejconfig";
    const fs::path future = directory.path() / "future.json";
    write_file(malformed, "{ not json");
    write_file(future, "{\"schema_version\": 2}");
    importer.import_file(qt_path(malformed));
    importer.import_file(qt_path(future)); // waits
    REQUIRE(loaded.size() == 1);
    CHECK(loaded[0] == "{ not json");

    importer.load_finished(); // importFailed from the model
    CHECK_FALSE(fs::exists(malformed));
    REQUIRE(loaded.size() == 2);
    importer.load_finished();
    CHECK_FALSE(fs::exists(future));
    CHECK(idles == 1);
}

TEST_CASE("importer.purge: stale files in the Inbox and the export folder go, and nothing else",
          "[integration][import]")
{
    TempDirectory directory;
    const fs::path documents = directory.path() / "Documents";
    const fs::path temporary = directory.path() / "tmp";
    fs::create_directories(documents / "Inbox");
    fs::create_directories(documents / "Kept");
    fs::create_directories(temporary / "ej-share");
    write_file(documents / "Inbox" / "old.ejconfig", "{}");
    write_file(documents / "Inbox" / "older.json", "{}");
    write_file(temporary / "ej-share" / std::string(bridge::CONFIG_EXPORT_FILE_NAME), "{}");
    write_file(documents / "Kept" / "keep.txt", "x");
    write_file(documents / "top.txt", "x");

    CHECK(bridge::purge_import_leftovers(qt_path(documents), qt_path(temporary)) == 3);
    CHECK_FALSE(fs::exists(documents / "Inbox" / "old.ejconfig"));
    CHECK(fs::exists(documents / "Kept" / "keep.txt"));
    CHECK(fs::exists(documents / "top.txt"));
    // Nothing there at all is not an error.
    CHECK(bridge::purge_import_leftovers(qt_path(directory.path() / "none"), qt_path(directory.path() / "nada")) == 0);
    CHECK(bridge::purge_import_leftovers(QString(), QString()) == 0);
}

TEST_CASE("importer.picker: PICKED is imported and deleted, CANCELLED is silent, FAILED gives one message",
          "[integration][import]")
{
    AppFixture app;
    TempDirectory directory;
    std::vector<std::string> loaded;
    bridge::ConfigImporter importer([&](const QString& path)
                                    { loaded.push_back(read_file(fs::path(path.toStdU16String()))); });
    QStringList failures;
    QObject::connect(&importer, &bridge::ConfigImporter::failed, &importer,
                     [&](const QStringList& m) { failures += m; });
    FakePicker picker;

    const fs::path file = directory.path() / "picked.ejconfig";
    write_file(file, "{\"schema_version\": 1}");
    picker.m_Result = {bridge::PickOutcome::PICKED, file};
    importer.pick_and_import(picker);
    REQUIRE(loaded.size() == 1);
    importer.load_finished();
    CHECK_FALSE(fs::exists(file));

    picker.m_Result = {bridge::PickOutcome::CANCELLED, {}};
    importer.pick_and_import(picker);
    CHECK(failures.isEmpty());
    CHECK(loaded.size() == 1);

    picker.m_Result = {bridge::PickOutcome::FAILED, {}};
    importer.pick_and_import(picker);
    CHECK(failures.size() == 1);
    picker.m_Result = {bridge::PickOutcome::UNAVAILABLE, {}};
    importer.pick_and_import(picker);
    CHECK(failures.size() == 2);
    CHECK(picker.m_Calls == 4);
}

TEST_CASE("exporter: the setup is written to the export folder, shared, and a failed write leaves nothing",
          "[integration][import]")
{
    AppFixture app;
    TempDirectory directory;
    const QString folder = qt_path(directory.path() / "ej-share");
    QString written;
    QString shared;
    bridge::ConfigExporter exporter(
        folder,
        [&](const QString& path)
        {
            written = path;
            write_file(fs::path(path.toStdU16String()), "{\"schema_version\": 1}");
        },
        [&](const QString& path) { shared = path; });
    QStringList failures;
    QObject::connect(&exporter, &bridge::ConfigExporter::failed, &exporter, [&](const QString& m) { failures += m; });

    // A stale file from an earlier run is removed first.
    fs::create_directories(directory.path() / "ej-share");
    write_file(directory.path() / "ej-share" / std::string(bridge::CONFIG_EXPORT_FILE_NAME), "stale");

    exporter.request();
    CHECK(exporter.busy());
    exporter.request(); // one at a time: ignored
    CHECK(written.endsWith(QString::fromUtf8(bridge::CONFIG_EXPORT_FILE_NAME.data())));
    exporter.export_finished(true, QString());
    CHECK(shared == written);
    CHECK_FALSE(exporter.busy());
    CHECK(failures.isEmpty());
    CHECK(read_file(fs::path(written.toStdU16String())) == "{\"schema_version\": 1}");

    shared.clear();
    exporter.request();
    exporter.export_finished(false, QStringLiteral("The setup could not be saved."));
    CHECK(shared.isEmpty());
    REQUIRE(failures.size() == 1);
    CHECK(failures[0] == "The setup could not be saved.");
    CHECK_FALSE(fs::exists(fs::path(written.toStdU16String())));
}
