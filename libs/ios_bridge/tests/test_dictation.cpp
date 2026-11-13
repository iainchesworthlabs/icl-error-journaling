// SPDX-License-Identifier: GPL-3.0-or-later
// Dictation through the bridge (PRD NOTE-07): dictation.start, dictation.words, dictation.stop, dictation.restart,
// dictation.silence, dictation.refused, dictation.background. A fake stands in for the phone's recogniser.
#include "error_journaling/dictation.hpp"
#include "error_journaling/platform_services.hpp"
#include "error_journaling/qml_bridge.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <chrono>
#include <memory>
#include <string>
#include <utility>

namespace bridge = ej::bridge;

namespace
{

// A recogniser the test drives: it records the starts and stops, and says words or ends a run when told to.
class FakeDictation final : public bridge::Dictation
{
public:
    [[nodiscard]] bool available() const noexcept override
    {
        return m_Available;
    }

    void start(bridge::DictationHandlers handlers) override
    {
        ++m_Starts;
        m_Handlers = std::move(handlers);
    }

    void stop() override
    {
        ++m_Stops;
    }

    void say(const std::string& text) const
    {
        m_Handlers.m_Text(text);
    }

    void end(bridge::DictationEnd how)
    {
        auto handlers = std::move(m_Handlers);
        m_Handlers = {};
        handlers.m_Ended(how);
    }

    bool m_Available = true;
    int m_Starts = 0;
    int m_Stops = 0;
    bridge::DictationHandlers m_Handlers;
};

// What the bridge says, in order.
struct Recorder
{
    explicit Recorder(bridge::QmlBridge& qml)
    {
        QObject::connect(&qml, &bridge::QmlBridge::dictationSegment, &qml, [this] { ++m_Segments; });
        QObject::connect(&qml, &bridge::QmlBridge::dictationText, &qml, [this](const QString& t) { m_Words.append(t); });
        QObject::connect(&qml, &bridge::QmlBridge::dictationEnded, &qml, [this](const QString& r) { m_Ended.append(r); });
    }

    int m_Segments = 0;
    QStringList m_Words;
    QStringList m_Ended;
};

struct AppFixture
{
    int m_Argc{0};
    QCoreApplication m_App{m_Argc, nullptr};
};

// Runs the event loop for `ms` milliseconds: the timers and the queued calls of the bridge.
void pump(int ms)
{
    QElapsedTimer clock;
    clock.start();
    while (clock.elapsed() < ms)
    {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
}

// The bridge is built with the desktop services (it needs the accessibility and haptic ones) and only the recogniser is
// replaced by the fake.
struct DesktopRig
{
    explicit DesktopRig(bool available = true)
    {
        auto services = bridge::make_platform_services();
        auto fake = std::make_unique<FakeDictation>();
        fake->m_Available = available;
        m_Fake = fake.get();
        services.m_Dictation = std::move(fake);
        m_Bridge = std::make_unique<bridge::QmlBridge>(std::move(services));
    }

    AppFixture m_App;
    FakeDictation* m_Fake = nullptr;
    std::unique_ptr<bridge::QmlBridge> m_Bridge;
};

// A run that has lasted long enough not to be taken for a recogniser that cannot listen.
void let_it_listen()
{
    pump(450);
}

} // namespace

TEST_CASE("dictation.start: unavailable on a device that cannot recognise on the device, and then nothing starts",
          "[unit][dictation]")
{
    DesktopRig rig(false);
    CHECK_FALSE(rig.m_Bridge->dictation_available());
    rig.m_Bridge->startDictation();
    CHECK(rig.m_Fake->m_Starts == 0);
    CHECK_FALSE(rig.m_Bridge->dictating());
}

TEST_CASE("dictation.start: the desktop stub is unavailable", "[unit][dictation]")
{
    AppFixture app;
    bridge::QmlBridge qml(bridge::make_platform_services());
    CHECK_FALSE(qml.dictation_available());
    qml.startDictation();
    CHECK_FALSE(qml.dictating());
}

TEST_CASE("dictation.start: a bridge without a recogniser reports it unavailable", "[unit][dictation]")
{
    AppFixture app;
    auto services = bridge::make_platform_services();
    services.m_Dictation.reset();
    bridge::QmlBridge qml(std::move(services));
    CHECK_FALSE(qml.dictation_available());
    qml.startDictation();
    CHECK_FALSE(qml.dictating());
}

TEST_CASE("dictation.words: each guess replaces the one before it, and a second tap ends it", "[unit][dictation]")
{
    DesktopRig rig;
    Recorder rec(*rig.m_Bridge);

    rig.m_Bridge->startDictation();
    CHECK(rig.m_Bridge->dictating());
    REQUIRE(rig.m_Fake->m_Starts == 1);
    CHECK(rec.m_Segments == 1);

    rig.m_Fake->say("mute");
    rig.m_Fake->say("mute delay near");
    REQUIRE(rec.m_Words.size() == 2);
    CHECK(rec.m_Words.at(0) == QStringLiteral("mute"));
    CHECK(rec.m_Words.at(1) == QStringLiteral("mute delay near"));

    rig.m_Bridge->startDictation(); // while listening: nothing
    CHECK(rig.m_Fake->m_Starts == 1);

    rig.m_Bridge->stopDictation();
    CHECK(rig.m_Fake->m_Stops == 1);
    // The recogniser sends its last words after it is told to stop, and then ends the run.
    CHECK(rig.m_Bridge->dictating());
    rig.m_Fake->say("mute delay near the stage");
    rig.m_Fake->end(bridge::DictationEnd::FINISHED);
    REQUIRE(rec.m_Words.size() == 3);
    CHECK(rec.m_Words.at(2) == QStringLiteral("mute delay near the stage"));
    CHECK_FALSE(rig.m_Bridge->dictating());
    REQUIRE(rec.m_Ended.size() == 1);
    CHECK(rec.m_Ended.at(0) == QStringLiteral("finished"));
}

TEST_CASE("dictation.stop: a recogniser that does not answer is closed after a moment", "[unit][dictation]")
{
    DesktopRig rig;
    Recorder rec(*rig.m_Bridge);
    rig.m_Bridge->startDictation();
    rig.m_Bridge->stopDictation();
    CHECK(rig.m_Bridge->dictating());
    pump(1700);
    CHECK_FALSE(rig.m_Bridge->dictating());
    CHECK(rec.m_Ended.size() == 1);
}

TEST_CASE("dictation.restart: a run the recogniser ends by itself is followed by another, in a new segment",
          "[unit][dictation]")
{
    DesktopRig rig;
    Recorder rec(*rig.m_Bridge);
    rig.m_Bridge->startDictation();
    let_it_listen();
    rig.m_Fake->say("first part");
    rig.m_Fake->end(bridge::DictationEnd::FINISHED); // a pause in the speech
    pump(50);
    CHECK(rig.m_Fake->m_Starts == 2);
    CHECK(rec.m_Segments == 2);
    CHECK(rig.m_Bridge->dictating());
    CHECK(rec.m_Ended.size() == 0);

    let_it_listen();
    rig.m_Fake->end(bridge::DictationEnd::NO_SPEECH);
    pump(50);
    CHECK(rig.m_Fake->m_Starts == 3);
    CHECK(rig.m_Bridge->dictating());
}

TEST_CASE("dictation.restart: a recogniser that ends at once, again and again, is a failure and does not spin",
          "[unit][dictation]")
{
    DesktopRig rig;
    Recorder rec(*rig.m_Bridge);
    rig.m_Bridge->startDictation();
    rig.m_Fake->end(bridge::DictationEnd::NO_SPEECH);
    REQUIRE(rec.m_Ended.size() == 1);
    CHECK(rec.m_Ended.at(0) == QStringLiteral("failed"));
    CHECK(rig.m_Fake->m_Starts == 1);
    CHECK_FALSE(rig.m_Bridge->dictating());
}

TEST_CASE("dictation.silence: nothing heard for the silence limit ends it, and speech starts the wait again",
          "[unit][dictation]")
{
    DesktopRig rig;
    Recorder rec(*rig.m_Bridge);
    rig.m_Bridge->set_dictation_silence_limit(std::chrono::milliseconds(600));
    rig.m_Bridge->startDictation();
    pump(450);
    rig.m_Fake->say("still talking"); // the wait starts again
    pump(450);
    CHECK(rig.m_Bridge->dictating());
    CHECK(rec.m_Ended.size() == 0);
    pump(400); // 850 ms without a word: past the limit
    CHECK(rig.m_Fake->m_Stops == 1);
    rig.m_Fake->end(bridge::DictationEnd::FINISHED);
    CHECK_FALSE(rig.m_Bridge->dictating());
    CHECK(rec.m_Ended.size() == 1);
}

TEST_CASE("dictation.silence: the limit is a minute", "[unit][dictation]")
{
    CHECK(bridge::QmlBridge::DICTATION_SILENCE_LIMIT == std::chrono::milliseconds(60'000));
}

TEST_CASE("dictation.refused: a refused permission, an unavailable recogniser and an error each end it once, with a reason",
          "[unit][dictation]")
{
    const std::pair<bridge::DictationEnd, const char*> cases[] = {{bridge::DictationEnd::DENIED, "denied"},
                                                                  {bridge::DictationEnd::UNAVAILABLE, "unavailable"},
                                                                  {bridge::DictationEnd::FAILED, "failed"}};
    for (const auto& [how, reason] : cases)
    {
        DYNAMIC_SECTION(reason)
        {
            DesktopRig rig;
            Recorder rec(*rig.m_Bridge);
            rig.m_Bridge->startDictation();
            rig.m_Fake->end(how);
            REQUIRE(rec.m_Ended.size() == 1);
            CHECK(rec.m_Ended.at(0) == QString::fromLatin1(reason));
            CHECK_FALSE(rig.m_Bridge->dictating());
            // And the person can tap again.
            rig.m_Bridge->startDictation();
            CHECK(rig.m_Fake->m_Starts == 2);
        }
    }
}

TEST_CASE("dictation.refused: words from a run that is over are ignored", "[unit][dictation]")
{
    DesktopRig rig;
    Recorder rec(*rig.m_Bridge);
    rig.m_Bridge->startDictation();
    const auto old_handlers = rig.m_Fake->m_Handlers;
    rig.m_Fake->end(bridge::DictationEnd::DENIED);
    old_handlers.m_Text("late words");
    old_handlers.m_Ended(bridge::DictationEnd::FINISHED);
    CHECK(rec.m_Words.size() == 0);
}

TEST_CASE("dictation.background: going to the background stops listening", "[unit][dictation]")
{
    DesktopRig rig;
    rig.m_Bridge->startDictation();
    rig.m_Bridge->setApplicationActive(false);
    CHECK(rig.m_Fake->m_Stops == 1);
    rig.m_Fake->end(bridge::DictationEnd::FINISHED);
    CHECK_FALSE(rig.m_Bridge->dictating());
}
