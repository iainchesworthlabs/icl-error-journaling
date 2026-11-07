// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/platform_services.hpp"
#include "error_journaling/qml_bridge.hpp"
#include "error_journaling/share_presenter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QCoreApplication>
#include <QList>
#include <QObject>
#include <QString>
#include <atomic>
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

struct Fakes
{
    FakeShare* m_Share = nullptr;
    FakeHaptics* m_Haptics = nullptr;
};

bridge::PlatformServices fake_services(Fakes& fakes)
{
    bridge::PlatformServices services;
    auto share = std::make_unique<FakeShare>();
    auto haptics = std::make_unique<FakeHaptics>();
    fakes.m_Share = share.get();
    fakes.m_Haptics = haptics.get();
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
    qml.hapticSuccess();

    CHECK(fakes.m_Haptics->m_Prepared == 1);
    REQUIRE(fakes.m_Haptics->m_Fired.size() == 2);
    CHECK(fakes.m_Haptics->m_Fired[0] == bridge::HapticKind::IMPACT);
    CHECK(fakes.m_Haptics->m_Fired[1] == bridge::HapticKind::SUCCESS);
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
