// SPDX-License-Identifier: GPL-3.0-or-later
// The Log screen (docs/plan/stage-4.md, 4.7), against the real Stage 3 view models: LogSession, BoardModel,
// UndoController and TimelineModel over the in-memory store and the manual gateway, so a case can look between
// "submitted" and "committed". The bridge is a small QObject with the names of the QmlBridge, which writes the haptics
// it is asked for into the same EventLog as the store, the observers and the confirmation, so the order of the feedback
// is the order of the log: haptic, then commit, then count and toast.
//
// Cases: log.loading_placeholder, log.tap_flow, log.slide_off, log.undo, log.note, log.template, log.banner,
// log.write_failed, log.recovery_notice, log.empty_tiles, log.empty_board, log.other, log.voiceover,
// log.skeleton_matches, log.opens, log.safe_area, log.text_scale, perf.press_path, perf.scene and perf.first_frame for
// the screen; and the hold on a tile (BRD-07, 4.17): longpress.opens_note, longpress.records_once, longpress.save,
// longpress.cancel, longpress.slow_commit, longpress.voiceover and, for a typed draft, longpress.draft_attaches,
// longpress.draft_undo, longpress.draft_tap, longpress.draft_slow_commit, longpress.draft_failed and
// longpress.note_action; and the double-tap that logs
// (4.17a): doubletap.logs, doubletap.window, doubletap.triple, doubletap.slide_off, doubletap.hint, doubletap.hold,
// doubletap.hold_then_tap and doubletap.voiceover.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort board_test_support.hpp first, so it is
// switched off.
// clang-format off
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_platform.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/recording_repository.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/template_list_model.hpp"
#include "viewmodel/timeline_model.hpp"
#include "viewmodel/undo_controller.hpp"

#include "board_test_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;
using viewmodel::BoardModel;
using viewmodel::LogSession;
using viewmodel::StartupResult;
using viewmodel::TemplateListModel;
using viewmodel::TimelineModel;
using viewmodel::UndoController;

namespace
{

constexpr int WRONG_RESPONSE_ROW = 0;
constexpr int MUTE_DELAY_ROW = 7;
constexpr int OTHER_ROW = 11;
constexpr int COVERED_BY_TOAST_FROM = 9;
constexpr int ROWS = 12;
/// A double-tap: the gap between its two taps, and the time that lets the window (Theme.motion.doubleTapMs, 300 ms) of
/// the last tap run out.
constexpr auto DOUBLE_TAP_GAP = 100ms;
constexpr auto WINDOW_RUN_OUT = 400ms;

const QString PLAYED_SONG = QStringLiteral("00000000-0000-4000-8000-0000000000c1");
const QString NORTH_SITE = QStringLiteral("00000000-0000-4000-8000-0000000000b1");

/// The part of the bridge the screen calls, with the names of `QmlBridge`: the haptics go through a `FakePlatform`,
/// which writes them into the event log; the board-visible calls are kept.
class FakeBridge : public QObject
{
    Q_OBJECT

public:
    explicit FakeBridge(FakePlatform& platform) : m_Platform(platform) {}

    Q_INVOKABLE void prepareHaptics() { m_Platform.prepare(); }
    Q_INVOKABLE void setBoardVisible(bool visible) { m_Visible.push_back(visible); }
    Q_INVOKABLE void hapticImpact(double /*pressTimestamp*/ = 0) { m_Platform.fire(FakePlatform::haptic_kind::IMPACT); }
    Q_INVOKABLE void hapticSuccess() { m_Platform.fire(FakePlatform::haptic_kind::SUCCESS); }

    std::vector<bool> m_Visible;

private:
    FakePlatform& m_Platform;
};

/// The part of the settings model the screen calls, with the names of `SettingsModel`: the templates for the chips, the
/// unit prefix of the card, `patternToField` and `previewTemplateLine`, which writes the line as the renderer does for a value (the sample
/// "Morning Medley" while it is blank, at the sample time 10:30).
class FakeSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QObject* templates READ templates CONSTANT)
    Q_PROPERTY(QString unitPrefix READ unit_prefix CONSTANT)
    // What the time sheet of the Edit entry sheet reads: a 24-hour clock without seconds.
    Q_PROPERTY(QString timeStyle READ time_style CONSTANT)
    Q_PROPERTY(bool timeSeconds READ time_seconds CONSTANT)

public:
    explicit FakeSettings(QObject* templates) : m_Templates(templates) {}

    [[nodiscard]] QObject* templates() const { return m_Templates; }
    [[nodiscard]] QString unit_prefix() const { return QStringLiteral("UNIT"); }
    [[nodiscard]] QString time_style() const { return QStringLiteral("24h"); }
    [[nodiscard]] bool time_seconds() const { return false; }

    Q_INVOKABLE QString previewTemplateLine(const QString& pattern, const QString& value, int /*localSeconds*/ = -1) const
    {
        QString line = pattern;
        line.replace(QStringLiteral("{value}"), value.trimmed().isEmpty() ? QStringLiteral("Morning Medley") : value.trimmed());
        return QStringLiteral("10:30 (Free Text) ") + line;
    }

    /// How the template editor writes a stored pattern: the answer slot is "[answer]".
    Q_INVOKABLE QString patternToField(const QString& pattern) const
    {
        QString shown = pattern;
        shown.replace(QStringLiteral("{value}"), QStringLiteral("[answer]"));
        return shown;
    }

private:
    QObject* m_Templates;
};

/// The configuration source, with the configuration in force from the start. It is the first base of the rig, so that
/// it is complete before any model reads it.
struct ConfiguredSource
{
    explicit ConfiguredSource(Configuration configuration) { config.set(std::move(configuration)); }

    FakeConfigurationSource config;
};

/// The sample header with the commit of the build, so that every field is filled in and the card has no strip under it
/// (HDR-09: an empty commit is listed there as still to fill).
HeaderValues complete_header()
{
    HeaderValues header = sample_header();
    header.m_SoftwareVersion = SoftwareVersion::parse("1.4.2", "a1b2c3d").value();
    return header;
}

struct Options
{
    bool m_Existing = false; ///< a log with a complete header (every field filled, the commit too) is in the store
    HeaderValues m_Header = complete_header();
    RecoveryReport m_Recovery;
    Configuration m_Configuration = sample_configuration();

    Options& existing()
    {
        m_Existing = true;
        return *this;
    }
    Options& recovery(RecoveryOutcome outcome)
    {
        m_Recovery.m_Outcome = outcome;
        m_Recovery.m_OpenLogEntries = 1;
        m_Recovery.m_RebuiltRecords = 12;
        return *this;
    }
    Options& configuration(Configuration configuration)
    {
        m_Configuration = std::move(configuration);
        return *this;
    }
    /// A header whose words are short, so that no line of the card wraps in any font (the font of a test machine is not
    /// the phone's, and some have no narrow glyphs at all).
    Options& compact_header()
    {
        m_Header.m_Facilitator = "Sam";
        m_Header.m_Location = LocationValue{std::nullopt, "Site"};
        return *this;
    }
    /// Names short enough for the two chips to share a row in any font, which the design has and the font of a test
    /// machine may not give.
    Options& short_templates()
    {
        m_Configuration.m_Templates.at(0).m_Name = "Song";
        m_Configuration.m_Templates.at(1).m_Name = "Guest";
        return *this;
    }
};

/// The view models of the Log screen over the in-memory store. The clock starts at Sat 7 Nov 2026 08:55 in Melbourne.
/// The members are in the order they depend on each other, so they go the other way.
class LogRig : public ConfiguredSource
{
public:
    explicit LogRig(Options options = {})
        : ConfiguredSource(std::move(options.m_Configuration)), m_Env(world.environment()),
          storage(world.m_Clock, &events), registry(m_Env, gateway), feedback(&events), platform(&events),
          session(m_Env, gateway, registry, config, feedback, StartupResult{options.m_Recovery}),
          board(config, session), undo(m_Env, config), clipboard(&events), timeline(m_Env, config, clipboard),
          settings(&templates), bridge(platform)
    {
        if (options.m_Existing)
        {
            REQUIRE(storage.memory().create_log(log_id, options.m_Header));
        }
        templates.refresh(config.configuration());
        timeline.follow(session);
        // The note that an undo gives back goes to the composer through the draft, as AppRoot wires it.
        undo.set_note_undo_target([this](const std::string& text) { session.set_draft(QString::fromStdString(text)); });
        // A draft that a held tile attached goes back with the undo of that tile, as AppRoot wires it.
        QObject::connect(&session, &LogSession::draftAttached, &undo,
                         [this](const QString& entryId, const QString& text)
                         {
                             if (const auto uuid = Uuid::parse(entryId.toStdString()))
                             {
                                 undo.note_attached(EntryId(*uuid), text.toStdString());
                             }
                         });
        QObject::connect(&undo, &UndoController::toastChanged, &undo, [this]
                         { events.record(EventKind::TOAST, undo.active() ? undo.text().toStdString() : "closed"); });
        QObject::connect(&board, &QAbstractItemModel::dataChanged, &board,
                         [this] { events.record(EventKind::MARK, "board"); });
        QObject::connect(&session, &LogSession::writeFailed, &session, [this](const QString& operation, const QString&)
                         { events.record(EventKind::WRITE_FAILED, operation.toStdString()); });
    }

    ~LogRig()
    {
        timeline.unbind();
        if (observed != nullptr)
        {
            undo.stop_observing(*observed);
        }
    }

    LogRig(const LogRig&) = delete;
    LogRig& operator=(const LogRig&) = delete;

    /// Reads the open log, as start-up does, and has the undo controller watch it.
    void load()
    {
        session.load();
        gateway.run_all();
        REQUIRE(session.open_log() != nullptr);
        observed = session.open_log();
        undo.observe(*observed);
        events.clear();
    }

    /// The live entries of the open log, in time order.
    [[nodiscard]] std::vector<Entry> entries()
    {
        std::vector<Entry> out;
        viewmodel::LogDocument* document = session.open_log();
        REQUIRE(document != nullptr);
        for (const Entry* entry : document->log().sorted_entries())
        {
            out.push_back(*entry);
        }
        return out;
    }

    [[nodiscard]] QVariantMap properties(bool with_insets = true)
    {
        QVariantMap map{
            {QStringLiteral("session"), QVariant::fromValue(static_cast<QObject*>(&session))},
            {QStringLiteral("board"), QVariant::fromValue(static_cast<QObject*>(&board))},
            {QStringLiteral("undo"), QVariant::fromValue(static_cast<QObject*>(&undo))},
            {QStringLiteral("timeline"), QVariant::fromValue(static_cast<QObject*>(&timeline))},
            {QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject*>(&settings))},
            {QStringLiteral("bridge"), QVariant::fromValue(static_cast<QObject*>(&bridge))}};
        if (with_insets)
        {
            map.insert(QStringLiteral("overrideInsets"), true);
            map.insert(QStringLiteral("overrideTop"), 59.0);
            map.insert(QStringLiteral("overrideBottom"), 34.0);
        }
        return map;
    }

    TestEnvironment world;
    viewmodel::Environment m_Env;
    EventLog events;
    MemoryStorage storage;
    ManualStorageGateway gateway{storage.context()};
    viewmodel::DocumentRegistry registry;
    FakeFeedbackSink feedback;
    FakePlatform platform;
    LogId log_id = sample_id<LogId>(1);
    LogSession session;
    BoardModel board;
    UndoController undo;
    FakeClipboard clipboard;
    TimelineModel timeline;
    TemplateListModel templates;
    FakeSettings settings;
    FakeBridge bridge;
    viewmodel::LogDocument* observed = nullptr;
};

/// The rig with the clock and the gateway of the view models attached, and the screen loaded.
struct Screen
{
    explicit Screen(LogRig& sim)
    {
        rig.attach(sim.world.m_Clock);
        rig.attach(sim.gateway);
        // The hold of a tile is timed by an animation: the driver has to tick between holds (the Timers of the screen, if any,
        // follow the same clock).
        rig.steady_time();
        item = rig.load(QStringLiteral("LogScreen"), sim.properties());
        REQUIRE(item != nullptr);
        rig.layout();
    }

    UiRig rig;
    QQuickItem* item = nullptr;

    [[nodiscard]] QQuickItem* named(const QString& name)
    {
        QQuickItem* found = rig.find_item(name);
        REQUIRE(found != nullptr);
        return found;
    }
    [[nodiscard]] QQuickItem* tile(int row) { return named(QStringLiteral("tile%1").arg(row)); }
    /// The item named `name` under `from`; fails the test when there is none, so no caller dereferences a null pointer.
    [[nodiscard]] QQuickItem* inside(QQuickItem* from, const QString& name)
    {
        REQUIRE(from != nullptr);
        QQuickItem* found = rig.find_item(name, from);
        REQUIRE(found != nullptr);
        return found;
    }
    [[nodiscard]] QString text_of(const QString& name) { return named(name)->property("text").toString(); }
    [[nodiscard]] QObject* child(QQuickItem* from, const QString& name)
    {
        auto* found = from->findChild<QObject*>(name);
        REQUIRE(found != nullptr);
        return found;
    }
    [[nodiscard]] bool badge_visible(int row) { return inside(tile(row), QStringLiteral("tileBadge"))->isVisible(); }
    [[nodiscard]] QString badge_text(int row)
    {
        return inside(tile(row), QStringLiteral("tileBadgeText"))->property("text").toString();
    }
    [[nodiscard]] QString last_line() { return text_of(QStringLiteral("lastBarLine")); }
    [[nodiscard]] bool toast_shown() { return named(QStringLiteral("logToast"))->property("active").toBool(); }
    [[nodiscard]] QString toast_text() { return text_of(QStringLiteral("toastText")); }

    /// A double-tap on the tile (two taps `DOUBLE_TAP_GAP` apart), the commit, and then the time that lets the window
    /// of the gesture run out, so that the next tap on the tile is the first of a new one.
    void double_tap_tile(LogRig& sim, int row)
    {
        rig.tap(tile(row));
        rig.advance(DOUBLE_TAP_GAP);
        rig.tap(tile(row));
        sim.gateway.run_all();
        rig.settle();
        rig.advance(WINDOW_RUN_OUT);
        rig.layout();
    }
    /// The hint of a single tap, null until the first one has made it.
    [[nodiscard]] QQuickItem* hint()
    {
        return rig.find_item(QStringLiteral("logTapHint"));
    }
    [[nodiscard]] bool hint_shown()
    {
        QQuickItem* found = hint();
        return found != nullptr && found->isVisible();
    }
};

std::optional<std::size_t> first_index(const EventLog& events, EventKind kind)
{
    const auto all = events.events();
    for (std::size_t i = 0; i < all.size(); ++i)
    {
        if (all[i].m_Kind == kind)
        {
            return i;
        }
    }
    return std::nullopt;
}

/// Every item under `from` with the object name, in tree order.
void collect(QQuickItem* from, const QString& name, std::vector<QQuickItem*>& out)
{
    if (from->objectName() == name)
    {
        out.push_back(from);
    }
    for (QQuickItem* child : from->childItems())
    {
        collect(child, name, out);
    }
}

const NoteEntry* note_of(const Entry& entry)
{
    return std::get_if<NoteEntry>(&entry.m_Body);
}

QString times(int count)
{
    return QString::fromUtf8("\xC3\x97") + QString::number(count);
}

}  // namespace

// ------------------------------------------------------------------------------------------------- loading

TEST_CASE(
    "log.loading_placeholder: while the log loads there are 12 shapes, no text and no touch, and the labels arrive "
    "without moving them",
    "[ui][log]")
{
    LogRig sim(Options().existing().short_templates().compact_header());
    Screen screen(sim);
    REQUIRE(sim.session.state() == LogSession::State::LOADING);

    std::vector<QRectF> shapes;
    for (int row = 0; row < ROWS; ++row)
    {
        QQuickItem* tile = screen.tile(row);
        QQuickItem* background = screen.inside(tile, QStringLiteral("tileBackground"));
        REQUIRE(background != nullptr);
        CHECK(background->isVisible());
        // No label, no count, no role: a shape.
        CHECK_FALSE(screen.inside(tile, QStringLiteral("tileLabel"))->isVisible());
        CHECK_FALSE(screen.inside(tile, QStringLiteral("tileBadge"))->isVisible());
        shapes.push_back(screen.rig.geometry(tile));
    }
    // The other parts draw shapes as well, with no words.
    CHECK_FALSE(screen.named(QStringLiteral("lastBarContent"))->isVisible());
    CHECK_FALSE(screen.named(QStringLiteral("noteInput"))->isEnabled());

    // A touch does nothing: no haptic, nothing sent to the session.
    screen.rig.tap(screen.tile(MUTE_DELAY_ROW));
    screen.rig.tap(screen.named(QStringLiteral("logHeaderCard")));
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.gateway.pending() == 0);

    sim.load();
    screen.rig.settle();
    REQUIRE(sim.session.state() == LogSession::State::OPEN);
    screen.rig.layout();

    // The labels are there now, and the shapes are where they were.
    CHECK(screen.inside(screen.tile(MUTE_DELAY_ROW), QStringLiteral("tileLabel"))->property("text").toString() ==
          QStringLiteral("Mute Delay"));
    CHECK(screen.inside(screen.tile(MUTE_DELAY_ROW), QStringLiteral("tileLabel"))->isVisible());
    for (int row = 0; row < ROWS; ++row)
    {
        CAPTURE(row);
        CHECK(screen.rig.geometry(screen.tile(row)) == shapes[static_cast<std::size_t>(row)]);
    }
    CHECK(screen.named(QStringLiteral("noteInput"))->isEnabled());
}

// ------------------------------------------------------------------------------------------------- the press path

TEST_CASE("log.tap_flow: the haptic comes first, then the commit, then the count, the toast and the Last bar",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    CHECK(screen.last_line() == QStringLiteral("No entries yet"));
    CHECK_FALSE(screen.toast_shown());

    // One tap logs nothing: no impact, no entry asked for.
    QQuickItem* mute = screen.tile(MUTE_DELAY_ROW);
    screen.rig.tap(mute);
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.gateway.pending() == 0);
    screen.rig.advance(DOUBLE_TAP_GAP);

    // The second tap's finger is down. The impact has fired and the entry has been asked for; nothing else has changed.
    const int finger = screen.rig.press(mute);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.events.count(EventKind::COMMIT) == 0);
    CHECK(sim.gateway.pending() == 1);
    CHECK_FALSE(screen.badge_visible(MUTE_DELAY_ROW));
    CHECK_FALSE(screen.toast_shown());
    CHECK(screen.last_line() == QStringLiteral("No entries yet"));
    screen.rig.release(finger);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);
    CHECK(sim.gateway.pending() == 1);

    // The commit.
    screen.rig.settle();
    CHECK(sim.events.count(EventKind::COMMIT) == 1);
    const auto haptic = first_index(sim.events, EventKind::HAPTIC);
    const auto commit = first_index(sim.events, EventKind::COMMIT);
    const auto board = first_index(sim.events, EventKind::MARK);
    const auto toast = first_index(sim.events, EventKind::TOAST);
    const auto confirmation = first_index(sim.events, EventKind::CONFIRMATION);
    REQUIRE((haptic && commit && board && toast && confirmation));
    CHECK(*haptic < *commit);
    CHECK(*commit < *board);
    CHECK(*commit < *toast);
    CHECK(*commit < *confirmation);

    CHECK(screen.badge_visible(MUTE_DELAY_ROW));
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("1"));
    CHECK(screen.toast_shown());
    CHECK(screen.toast_text() == QStringLiteral("Mute Delay logged at 08:55"));
    CHECK(screen.last_line().contains(QStringLiteral("Mute Delay")));
    CHECK(screen.text_of(QStringLiteral("lastBarCount")) == QStringLiteral("1 today"));

    // Another double-tap on the same tile, while the toast shows: the count and the toast say two, and the toast stays
    // open.
    screen.rig.advance(WINDOW_RUN_OUT);
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("2"));
    CHECK(screen.toast_text().contains(times(2)));
    CHECK(screen.toast_shown());
    CHECK(sim.events.count(EventKind::HAPTIC) == 2);
    CHECK(sim.entries().size() == 2);
}

TEST_CASE("log.tap_flow: the screen counts nothing itself, it shows what the models say", "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    // An entry that did not come through the screen at all.
    Entry entry = sample_error(1, 10);
    entry.m_Body = ErrorEntry{sim.config.configuration().m_ErrorTypes.front().m_Id, std::nullopt, "Wrong Response"};
    viewmodel::LogDocument* document = sim.session.open_log();
    REQUIRE(document != nullptr);
    REQUIRE(document->add_entry(std::move(entry), viewmodel::LogChange::Origin::USER,
                                [](const Result<void, StorageError>&) {}));
    sim.gateway.run_all();
    screen.rig.settle();

    CHECK(screen.badge_text(WRONG_RESPONSE_ROW) == QStringLiteral("1"));
    CHECK(screen.text_of(QStringLiteral("lastBarCount")) == QStringLiteral("1 today"));
    // The screen has no counter of its own to be out of step.
    CHECK_FALSE(screen.item->property("count").isValid());
    CHECK_FALSE(screen.item->property("entryCount").isValid());
}

TEST_CASE("log.slide_off: the second touch slides off the tile and lifts: one haptic and one entry, from that touch",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);

    screen.rig.tap(tile);
    screen.rig.advance(DOUBLE_TAP_GAP);
    const int finger = screen.rig.press(tile);
    screen.rig.move(finger, tile, QPointF(tile->width() + 60, tile->height() / 2));
    screen.rig.release(finger);
    sim.gateway.run_all();
    screen.rig.settle();

    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.entries().size() == 1);
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("1"));
    CHECK(screen.toast_shown());
}

TEST_CASE("log.undo: Undo removes the latest entry, closes the toast and the count falls", "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    REQUIRE(screen.toast_shown());
    REQUIRE(screen.badge_visible(MUTE_DELAY_ROW));
    sim.events.clear();

    screen.rig.tap(screen.named(QStringLiteral("toastUndo")));
    sim.gateway.run_all();
    screen.rig.settle();

    CHECK(sim.entries().empty());
    CHECK_FALSE(sim.undo.active());
    CHECK_FALSE(screen.toast_shown());
    CHECK_FALSE(screen.badge_visible(MUTE_DELAY_ROW));
    CHECK(screen.last_line() == QStringLiteral("No entries yet"));
    // An undo gives no haptic of its own.
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
}

TEST_CASE("log.voiceover: activating a tile records once and fires the impact first, and the toast is announced",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QSignalSpy announced(screen.named(QStringLiteral("logToast")), SIGNAL(announced(QString)));

    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);
    REQUIRE(accessible_press(tile));
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.gateway.pending() == 1);
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(32ms);

    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);
    CHECK(announced.count() == 1);
    CHECK(announced.first().first().toString() == QStringLiteral("Mute Delay logged at 08:55"));
    // One activation is the double-tap of VoiceOver itself: no second is asked for, and no hint follows.
    screen.rig.advance(WINDOW_RUN_OUT);
    CHECK(sim.entries().size() == 1);
    CHECK_FALSE(screen.hint_shown());
    // The name carries the count once there is one.
    CHECK(tile->property("accessibleName").toString().contains(QStringLiteral("Mute Delay")));
}

TEST_CASE("log.other: the Other tile opens the picker and never records", "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QSignalSpy other(screen.item, SIGNAL(otherRequested()));

    // One tap opens the picker (Other is a navigation, not a log, so it does not wait for a second tap), and says no
    // hint.
    screen.rig.tap(screen.tile(OTHER_ROW));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(other.count() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.entries().empty());
    screen.rig.advance(WINDOW_RUN_OUT);
    CHECK_FALSE(screen.hint_shown());

    // VoiceOver: the same.
    REQUIRE(accessible_press(screen.tile(OTHER_ROW)));
    sim.gateway.run_all();
    CHECK(other.count() == 2);
    CHECK(sim.entries().empty());
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);

    // A finger that slides off Other opens nothing.
    QQuickItem* tile = screen.tile(OTHER_ROW);
    const int finger = screen.rig.press(tile);
    screen.rig.move(finger, tile, QPointF(tile->width() + 60, tile->height() / 2));
    screen.rig.release(finger);
    CHECK(other.count() == 2);
}

TEST_CASE("log.empty_tiles: an empty tile records nothing and asks for the board setup at its position", "[ui][log]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.resize(9);
    LogRig sim(Options().existing().configuration(std::move(configuration)));
    sim.load();
    Screen screen(sim);
    QSignalSpy setup(screen.item, SIGNAL(boardSetupRequested(int)));

    for (const int row : {9, 10})
    {
        CAPTURE(row);
        QQuickItem* tile = screen.tile(row);
        CHECK(screen.inside(tile, QStringLiteral("tileEmptySlot"))->isVisible());
        CHECK(screen.inside(tile, QStringLiteral("tileAddIcon"))->isVisible());
        CHECK_FALSE(tile->property("interactive").toBool());
        CHECK(tile->property("addable").toBool());
        CHECK(tile->property("accessibleName").toString() ==
              QStringLiteral("Position %1, empty. Add error type").arg(row + 1));
        screen.rig.tap(tile);
        REQUIRE(setup.count() == 1);
        CHECK(setup.takeFirst().at(0).toInt() == row + 1);
        // VoiceOver's double tap is the same tap.
        CHECK(accessible_press(tile));
        REQUIRE(setup.count() == 1);
        CHECK(setup.takeFirst().at(0).toInt() == row + 1);
    }
    sim.gateway.run_all();
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.entries().empty());
    // A board with types is not the empty board.
    CHECK(screen.rig.find_item(QStringLiteral("logEmptyBoard")) == nullptr);
    // The real tiles still work.
    screen.double_tap_tile(sim, 0);
    CHECK(sim.entries().size() == 1);
}

TEST_CASE("log.empty_board: a board with no types says so and leads to the board setup", "[ui][log]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.clear();
    LogRig sim(Options().existing().configuration(std::move(configuration)));
    sim.load();
    Screen screen(sim);
    QSignalSpy setup(screen.item, SIGNAL(boardSetupRequested(int)));

    QQuickItem* card = screen.named(QStringLiteral("logEmptyBoard"));
    CHECK(card->isVisible());
    CHECK(screen.named(QStringLiteral("logEmptyBoardTitle"))->property("text").toString() ==
          QStringLiteral("Your board is empty"));
    // Over the board, inside its width, and above Other's row.
    QQuickItem* grid = screen.named(QStringLiteral("logBoard"));
    const QRectF on_screen = card->mapRectToScene(QRectF(0, 0, card->width(), card->height()));
    const QRectF board = grid->mapRectToScene(QRectF(0, 0, grid->width(), grid->height()));
    CHECK(board.contains(on_screen));
    CHECK(on_screen.bottom() <= screen.tile(9)->mapToScene(QPointF(0, 0)).y());

    QQuickItem* button = screen.named(QStringLiteral("logAddErrorTypes"));
    screen.rig.tap(button);
    REQUIRE(setup.count() == 1);
    CHECK(setup.takeFirst().at(0).toInt() == 0);
    CHECK(accessible_press(button));
    CHECK(setup.count() == 1);

    // Every element of the card is reachable and named, and the empty tiles under it are buttons.
    expect_clean(check_everything(screen.rig, [](const Node& node)
                                  { return node.m_ObjectName.startsWith(QStringLiteral("tile")) ? 88.0 : 44.0; }));
    sim.gateway.run_all();
    CHECK(sim.entries().empty());
}

// ------------------------------------------------------------------------------------------------- notes

TEST_CASE("log.note: type and Add, the entry appears after the commit, the field clears, Undo returns the text",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* input = screen.named(QStringLiteral("noteInput"));

    QMetaObject::invokeMethod(screen.named(QStringLiteral("logComposer")), "focusField");
    screen.rig.type(QStringLiteral("The robot paused twice"));
    // The draft follows the typing, and the screen keeps nothing.
    CHECK(sim.session.draft() == QStringLiteral("The robot paused twice"));

    screen.rig.tap(screen.named(QStringLiteral("addButton")));
    // Submitted, not committed: the field still has the text and there is no entry yet.
    CHECK(sim.gateway.pending() >= 1);
    CHECK(input->property("text").toString() == QStringLiteral("The robot paused twice"));
    CHECK(sim.timeline.entry_count() == 0);
    CHECK_FALSE(screen.toast_shown());

    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.timeline.entry_count() == 1);
    const std::vector<Entry> saved_entries = sim.entries();
    REQUIRE(saved_entries.size() == 1);
    const NoteEntry* saved_note = note_of(saved_entries.front());
    REQUIRE(saved_note != nullptr);
    CHECK(saved_note->m_Value == "The robot paused twice");
    CHECK(input->property("text").toString().isEmpty());
    CHECK(sim.session.draft().isEmpty());
    CHECK(screen.toast_shown());
    CHECK(screen.toast_text() == QStringLiteral("Note added at 08:55"));
    CHECK(screen.text_of(QStringLiteral("lastBarCount")) == QStringLiteral("1 today"));

    // The success haptic is the sink's, after the commit; the screen fires none of its own, so there is no second one.
    const auto commit = first_index(sim.events, EventKind::COMMIT);
    const auto saved = first_index(sim.events, EventKind::CONFIRMATION);
    REQUIRE((commit && saved));
    CHECK(*commit < *saved);
    CHECK(sim.events.count(EventKind::HAPTIC, "success") == 0);

    // Undo puts the text back in the composer, with the cursor at its end.
    screen.rig.tap(screen.named(QStringLiteral("toastUndo")));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().empty());
    CHECK(input->property("text").toString() == QStringLiteral("The robot paused twice"));
    CHECK(input->property("cursorPosition").toInt() == input->property("length").toInt());
    CHECK(sim.session.draft() == QStringLiteral("The robot paused twice"));
}

TEST_CASE("log.note: Return adds the note, and an empty note does nothing", "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    screen.rig.tap(screen.named(QStringLiteral("addButton")));
    CHECK(sim.gateway.pending() == 0);

    QMetaObject::invokeMethod(screen.named(QStringLiteral("logComposer")), "focusField");
    screen.rig.type(QStringLiteral("Visitor asked about the robot"));
    screen.rig.key(Qt::Key_Return);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
}

TEST_CASE("log.template: a chip opens the value prompt, Add records the template and the value, an empty value is "
          "refused inline",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    std::vector<QQuickItem*> chips;
    collect(screen.item, QStringLiteral("chip"), chips);
    REQUIRE(chips.size() == 2);
    CHECK(chips[0]->property("text").toString() == QStringLiteral("Played song"));
    // The icons of the chips come from the templates' `icon` role.
    CHECK(chips[0]->property("iconKind").toString() == QStringLiteral("music"));
    CHECK(chips[1]->property("iconKind").toString() == QStringLiteral("speech"));

    QQuickItem* sheet = screen.named(QStringLiteral("logTemplateSheet"));
    CHECK_FALSE(sheet->property("isOpen").toBool());
    screen.rig.tap(chips[0]);
    CHECK(sheet->property("isOpen").toBool());
    screen.rig.advance(400ms);

    // The prompt of the template.
    QQuickItem* field = screen.named(QStringLiteral("templateValueField"));
    CHECK(field->property("label").toString() == QStringLiteral("Which song?"));
    CHECK(sheet->property("accessibleName").toString() == QStringLiteral("Played song"));
    // The line the note will have, as the settings model writes it, with the sample value while the field is blank.
    CHECK(screen.text_of(QStringLiteral("templateValuePreviewText")) ==
          QStringLiteral("10:30 (Free Text) Played Morning Medley"));
    // The footnote names the line as the template editor writes it: [answer], not the stored {value}.
    CHECK(screen.named(QStringLiteral("templateValuePreview"))->property("footer").toString() ==
          QStringLiteral("Saved with the template “Played song”, as “Played [answer]”."));

    // Add with nothing typed: refused, the sheet stays, nothing is recorded.
    screen.rig.tap(screen.named(QStringLiteral("navTrailing")));
    sim.gateway.run_all();
    CHECK(sheet->property("isOpen").toBool());
    CHECK_FALSE(field->property("errorText").toString().isEmpty());
    CHECK(sim.entries().empty());

    // Typing clears the message; Add records the template id and the value, and the sheet closes.
    screen.rig.type(QStringLiteral("Morning Medley"));
    CHECK(field->property("text").toString() == QStringLiteral("Morning Medley"));
    CHECK(field->property("errorText").toString().isEmpty());
    CHECK(screen.text_of(QStringLiteral("templateValuePreviewText")) ==
          QStringLiteral("10:30 (Free Text) Played Morning Medley"));
    screen.rig.tap(screen.named(QStringLiteral("navTrailing")));
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(400ms);
    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    const NoteEntry* note = note_of(entries.front());
    REQUIRE(note != nullptr);
    REQUIRE(note->m_Template.has_value());
    CHECK(QString::fromStdString(note->m_Template->format()) == PLAYED_SONG);
    CHECK(note->m_Value == "Morning Medley");
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(screen.toast_text() == QStringLiteral("Note added at 08:55"));
    // The free-text draft was not touched.
    CHECK(sim.session.draft().isEmpty());
}

TEST_CASE("log.template: Cancel leaves everything as it was", "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    std::vector<QQuickItem*> chips;
    collect(screen.item, QStringLiteral("chip"), chips);
    REQUIRE(chips.size() == 2);

    screen.rig.tap(chips[1]);
    screen.rig.advance(400ms);
    QQuickItem* sheet = screen.named(QStringLiteral("logTemplateSheet"));
    REQUIRE(sheet->property("isOpen").toBool());
    CHECK(screen.named(QStringLiteral("templateValueField"))->property("label").toString() ==
          QStringLiteral("Who visited?"));
    screen.rig.tap(screen.named(QStringLiteral("navLeading")));
    screen.rig.advance(400ms);
    CHECK_FALSE(sheet->property("isOpen").toBool());
    sim.gateway.run_all();
    CHECK(sim.entries().empty());
}

// ------------------------------------------------------------------------------------------------- the messages

TEST_CASE("log.banner: an empty header lists the missing fields in the card, taps still work, filling a field removes "
          "its name",
          "[ui][log]")
{
    LogRig sim; // a first start: today's log with three empty fields, and no commit
    sim.load();
    Screen screen(sim);
    QQuickItem* card = screen.named(QStringLiteral("logHeaderCard"));

    CHECK(card->property("missingCount").toInt() == 4);
    CHECK(card->property("missingText").toString() == QStringLiteral("UNIT number, Location, Version, Commit"));
    CHECK(screen.text_of(QStringLiteral("lastBarLine")) == QStringLiteral("No entries yet"));

    // The card shows the prompts, in the design's words.
    CHECK(card->property("unitPrompt").toString() == QString::fromUtf8("UNIT \xE2\x80\x94"));

    // Nothing is blocked.
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    CHECK(sim.entries().size() == 1);

    REQUIRE(sim.session.correctHeader(QStringLiteral("unit_number"), QStringLiteral("27")));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(card->property("missingCount").toInt() == 3);
    CHECK(card->property("missingText").toString() == QStringLiteral("Location, Version, Commit"));

    REQUIRE(sim.session.correctHeader(QStringLiteral("location"), NORTH_SITE));
    REQUIRE(sim.session.correctHeader(QStringLiteral("software_version"), QStringLiteral("1.4.2")));
    sim.gateway.run_all();
    screen.rig.settle();
    // The commit is the one thing still to fill, and it is only a reminder: the card says so and the log goes on.
    CHECK(card->property("missingCount").toInt() == 1);
    CHECK(card->property("missingText").toString() == QStringLiteral("Commit"));
    CHECK(card->property("unitText").toString() == QStringLiteral("UNIT-27"));
    QQuickItem* strip = screen.named(QStringLiteral("headerStrip"));
    CHECK(strip->isVisible());
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    CHECK(sim.entries().size() == 2);

    REQUIRE(sim.session.correctVersion(QStringLiteral("1.4.2"), QStringLiteral("A1B2C3D")));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(card->property("missingCount").toInt() == 0);
    CHECK_FALSE(strip->isVisible());
    // The card shows the version alone, as before: the commit is for the log and the details.
    CHECK(card->property("versionText").toString() == QStringLiteral("1.4.2"));
}

TEST_CASE("log.write_failed: a failing commit shows the banner, no count, no toast and no confirmation, and the next "
          "tap works",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* banner = screen.named(QStringLiteral("logWriteFailedBanner"));
    CHECK_FALSE(banner->isVisible());
    const QRectF board_before = screen.rig.geometry(screen.named(QStringLiteral("logBoard")));

    sim.storage.repository().fail_next(RepositoryOp::ADD_ENTRY);
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);

    CHECK(banner->isVisible());
    // The banner names the type that was not saved (`session.recordFailed`), and there is one banner for it.
    CHECK(screen.text_of(QStringLiteral("bannerTitle")) == QString::fromUtf8("Mute Delay wasn\xE2\x80\x99t saved"));
    CHECK(screen.text_of(QStringLiteral("bannerMessage")) ==
          QStringLiteral("Nothing was added to the log. Tap Mute Delay again to log it."));
    CHECK(banner->property("floating").toBool());
    CHECK(sim.events.count(EventKind::WRITE_FAILED, "recordError") == 1);
    CHECK(sim.events.count(EventKind::CONFIRMATION) == 0);
    CHECK(sim.entries().empty());
    CHECK_FALSE(screen.badge_visible(MUTE_DELAY_ROW));
    CHECK_FALSE(screen.toast_shown());
    // The impact answered the finger, which is what it is for; there is no success haptic.
    CHECK(sim.events.count(EventKind::HAPTIC, "success") == 0);

    // The board did not move for it: the banner floats over the header card.
    CHECK(screen.rig.geometry(screen.named(QStringLiteral("logBoard"))) == board_before);
    CHECK(screen.rig.geometry(banner).top() == 59.0);

    // The next tap works, and the old complaint goes with it.
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    CHECK(sim.entries().size() == 1);
    CHECK_FALSE(banner->isVisible());
    CHECK(screen.toast_shown());
}

TEST_CASE("log.write_failed: the banner can be closed, and a failed undo shows it too", "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* banner = screen.named(QStringLiteral("logWriteFailedBanner"));

    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    sim.storage.repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);
    screen.rig.tap(screen.named(QStringLiteral("toastUndo")));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(banner->isVisible());
    CHECK(screen.text_of(QStringLiteral("bannerMessage")).contains(QStringLiteral("could not be undone")));
    CHECK(sim.entries().size() == 1);

    // A frame later, as a finger is.
    screen.rig.layout();
    screen.rig.tap(screen.inside(banner, QStringLiteral("bannerClose")));
    CHECK_FALSE(banner->isVisible());
}

TEST_CASE("log.recovery_notice: a notice shows once, with its action, and can be closed", "[ui][log]")
{
    LogRig sim(Options().existing().recovery(RecoveryOutcome::REBUILT));
    sim.load();
    Screen screen(sim);
    QQuickItem* banner = screen.named(QStringLiteral("logRecoveryBanner"));
    QSignalSpy review(screen.item, SIGNAL(reviewRequested()));

    REQUIRE_FALSE(sim.session.recovery_notice().isEmpty());
    CHECK(banner->isVisible());
    CHECK(screen.inside(banner, QStringLiteral("bannerMessage"))->property("text").toString() ==
          sim.session.recovery_notice());
    // The tiles under it work.
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    CHECK(sim.entries().size() == 1);

    screen.rig.tap(screen.inside(banner, QStringLiteral("bannerClose")));
    CHECK_FALSE(banner->isVisible());
    CHECK(review.count() == 0);
}

TEST_CASE("log.recovery_notice: the action opens Review and closes the notice; a clean start shows none", "[ui][log]")
{
    {
        LogRig sim(Options().existing().recovery(RecoveryOutcome::REPLAYED));
        sim.load();
        Screen screen(sim);
        QQuickItem* banner = screen.named(QStringLiteral("logRecoveryBanner"));
        QSignalSpy review(screen.item, SIGNAL(reviewRequested()));
        REQUIRE(banner->isVisible());
        screen.rig.tap(screen.inside(banner, QStringLiteral("bannerAction")));
        CHECK(review.count() == 1);
        CHECK_FALSE(banner->isVisible());
    }
    {
        LogRig sim(Options().existing());
        sim.load();
        Screen screen(sim);
        CHECK_FALSE(screen.named(QStringLiteral("logRecoveryBanner"))->isVisible());
    }
}

// ------------------------------------------------------------------------------------------------- the rest of the
// screen

TEST_CASE("log.opens: the header card opens Today's details, the Last bar opens Review, and the board is announced to "
          "the haptic policy",
          "[ui][log]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QSignalSpy details(screen.item, SIGNAL(todaysDetailsRequested()));
    QSignalSpy review(screen.item, SIGNAL(reviewRequested()));

    screen.rig.tap(screen.named(QStringLiteral("logHeaderCard")));
    CHECK(details.count() == 1);
    CHECK(review.count() == 0);
    screen.rig.tap(screen.named(QStringLiteral("logLastBar")));
    CHECK(review.count() == 1);
    CHECK(details.count() == 1);
    // Neither is a capture: no haptic, no entry.
    sim.gateway.run_all();
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.entries().empty());

    // The header card reads the values of the session.
    QQuickItem* card = screen.named(QStringLiteral("logHeaderCard"));
    CHECK(card->property("unitText").toString() == QStringLiteral("UNIT-27"));
    CHECK(card->property("locationText").toString() == QStringLiteral("North Site"));
    CHECK(card->property("versionText").toString() == QStringLiteral("1.4.2"));
    CHECK(card->property("facilitatorText").toString() == QStringLiteral("Sam Example"));

    // The haptic policy is told that the board is on screen, and when it is not.
    REQUIRE_FALSE(sim.bridge.m_Visible.empty());
    CHECK(sim.bridge.m_Visible.back());
    screen.item->setVisible(false);
    CHECK_FALSE(sim.bridge.m_Visible.back());
    screen.item->setVisible(true);
    CHECK(sim.bridge.m_Visible.back());
}

TEST_CASE("log.skeleton_matches: the shapes of the placeholder are where the real parts are, within half a point",
          "[ui][log]")
{
    const std::vector<QString> blocks = {QStringLiteral("logHeaderCard"), QStringLiteral("logComposer"),
                                         QStringLiteral("logLastBar"), QStringLiteral("logBoard")};

    // The loading screen, over a store that already has a log with every header field filled in. The chips are short,
    // so that they share a row as they do in the design (the font of a test machine is not the phone's).
    LogRig sim(Options().existing().short_templates().compact_header());
    Screen screen(sim);
    REQUIRE(sim.session.state() == LogSession::State::LOADING);
    std::vector<QRectF> shapes;
    for (const QString& name : blocks)
    {
        shapes.push_back(screen.rig.geometry(screen.named(name)));
    }
    for (int row = 0; row < ROWS; ++row)
    {
        shapes.push_back(screen.rig.geometry(screen.tile(row)));
    }

    sim.load();
    screen.rig.settle();
    screen.rig.layout();
    std::vector<QRectF> real;
    for (const QString& name : blocks)
    {
        real.push_back(screen.rig.geometry(screen.named(name)));
    }
    for (int row = 0; row < ROWS; ++row)
    {
        real.push_back(screen.rig.geometry(screen.tile(row)));
    }

    REQUIRE(shapes.size() == real.size());
    for (std::size_t i = 0; i < shapes.size(); ++i)
    {
        CAPTURE(i);
        CHECK(std::abs(shapes[i].x() - real[i].x()) <= 0.5);
        CHECK(std::abs(shapes[i].y() - real[i].y()) <= 0.5);
        CHECK(std::abs(shapes[i].width() - real[i].width()) <= 0.5);
        CHECK(std::abs(shapes[i].height() - real[i].height()) <= 0.5);
    }
}

TEST_CASE("log.geometry: the cards and the board sit where the design has them", "[ui][log]")
{
    LogRig sim(Options().existing().short_templates());
    sim.load();
    Screen screen(sim);

    const QRectF header = screen.rig.geometry(screen.named(QStringLiteral("logHeaderCard")));
    const QRectF composer = screen.rig.geometry(screen.named(QStringLiteral("logComposer")));
    const QRectF last = screen.rig.geometry(screen.named(QStringLiteral("logLastBar")));
    const QRectF board = screen.rig.geometry(screen.named(QStringLiteral("logBoard")));
    // 59 pt of status bar, 16 pt margins, 12 pt between the cards, and the board of three columns of 118 pt and four
    // rows of 104 pt with 8 pt gaps: 440 pt. What the text inside the cards needs depends on the font, so the heights
    // of the cards are not compared with the design here.
    CHECK(header.top() == 59.0);
    CHECK(header.left() == 16.0);
    CHECK(header.width() == 370.0);
    CHECK(composer.top() == header.bottom() + 12.0);
    CHECK(last.top() == composer.bottom() + 12.0);
    CHECK(last.height() == 44.0);
    CHECK(board.top() == last.bottom() + 12.0);
    CHECK(board.left() == 16.0);
    CHECK(board.width() == 370.0);
    CHECK(board.height() == 440.0);
    for (int row = 0; row < ROWS; ++row)
    {
        CAPTURE(row);
        const QRectF tile = screen.rig.geometry(screen.tile(row));
        CHECK(tile.width() == 118.0);
        CHECK(tile.height() == 104.0);
        CHECK(tile.left() == 16.0 + (row % 3) * (118.0 + 8.0));
        CHECK(tile.top() == board.top() + (row / 3) * (104.0 + 8.0));
    }

    // The toast floats 12 pt above the 83 pt tab bar, across the foot of the board, and moves nothing.
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    screen.rig.advance(400ms);
    const QRectF toast = screen.rig.geometry(screen.named(QStringLiteral("logToast")));
    CHECK(toast.bottom() == 874.0 - 83.0 - 12.0);
    CHECK(toast.left() == 16.0);
    CHECK(toast.width() == 370.0);
    CHECK(screen.rig.geometry(screen.named(QStringLiteral("logBoard"))) == board);

    // Where the screen is given less than the window, the toast stays above what it is told is the tab bar.
    screen.item->setProperty("bottomClearance", 0.0);
    screen.rig.layout();
    CHECK(screen.rig.geometry(screen.named(QStringLiteral("logToast"))).bottom() == 874.0 - 12.0);
}

TEST_CASE("log.safe_area: nothing interactive is in the insets, at 59 and 34 and at 79 and 54", "[ui][log]")
{
    LogRig sim(Options().existing().short_templates());
    sim.load();
    Screen screen(sim);
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    screen.rig.advance(400ms);

    expect_clean(check_safe_area(screen.rig, 59, 34));

    screen.item->setProperty("overrideTop", 79.0);
    screen.item->setProperty("overrideBottom", 54.0);
    screen.rig.layout();
    expect_clean(check_safe_area(screen.rig, 79, 54));
}

TEST_CASE("log.text_scale: names, targets, reading order and text are clean at 1.0, 1.3 and 3.1", "[ui][log]")
{
    LogRig sim(Options().existing().short_templates());
    sim.load();
    Screen screen(sim);

    // Tiles are 88 pt at least; everything else is 44.
    expect_clean(check_everything(screen.rig, [](const Node& node)
                                  { return node.m_ObjectName.startsWith(QStringLiteral("tile")) ? 88.0 : 44.0; }));
}

TEST_CASE("log.accessibility: the tree is named and reads in order with the messages and the toast showing, in light "
          "and dark",
          "[ui][log]")
{
    LogRig sim(Options().existing().short_templates().recovery(RecoveryOutcome::REBUILT));
    sim.load();
    Screen screen(sim);

    // The order a screen reader goes in, from the tree: the header card, the notice, the composer, the Last bar, the
    // board by row, and the toast after them. The generic check of the rig compares rectangles, which cannot hold for a
    // notice and a toast that float over the card and over the foot of the board, so the order is stated here.
    const auto order = [&screen]
    {
        std::vector<QString> names;
        for (const Node& node : screen.rig.accessible_tree())
        {
            if (node.m_Role != 0 && node.m_Visible)
            {
                names.push_back(node.m_ObjectName);
            }
        }
        return names;
    };
    const auto index = [](const std::vector<QString>& names, const QString& name)
    {
        const auto found = std::ranges::find(names, name);
        return found == names.end() ? -1 : static_cast<int>(found - names.begin());
    };

    // Failure banner and toast both showing.
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    sim.storage.repository().fail_next(RepositoryOp::SET_ENTRY_DELETED);
    screen.rig.tap(screen.named(QStringLiteral("toastUndo")));
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(400ms);
    REQUIRE(screen.named(QStringLiteral("logWriteFailedBanner"))->isVisible());
    REQUIRE(screen.toast_shown());

    for (const bool dark : {false, true})
    {
        CAPTURE(dark);
        screen.rig.set_dark(dark);
        screen.rig.layout();
        expect_clean(check_names(screen.rig));
        expect_clean(check_targets(screen.rig, [](const Node& node)
                                   { return node.m_ObjectName.startsWith(QStringLiteral("tile")) ? 88.0 : 44.0; }));

        const std::vector<QString> names = order();
        std::vector<QString> expected = {QStringLiteral("logHeaderCard"), QStringLiteral("bannerCard"),
                                         QStringLiteral("noteInput"), QStringLiteral("addButton"),
                                         QStringLiteral("logLastBar")};
        for (int row = 0; row < ROWS; ++row)
        {
            expected.push_back(QStringLiteral("tile%1").arg(row));
        }
        expected.push_back(QStringLiteral("logToast"));
        int previous = -1;
        for (const QString& name : expected)
        {
            CAPTURE(name);
            const int at = index(names, name);
            CHECK(at > previous);
            previous = std::max(previous, at);
        }
    }
}

TEST_CASE("log.accessibility: with nothing floating over it the generic reading order holds", "[ui][log]")
{
    LogRig sim(Options().existing().short_templates());
    sim.load();
    Screen screen(sim);
    expect_clean(check_reading_order(screen.rig));
}

// ------------------------------------------------------------------------------------------------- performance

// ------------------------------------------------------------------------------------------------- the hold

namespace
{

/// Beyond the 500 ms of `Tile.holdMs`, with a frame to spare.
constexpr auto HOLD = 600ms;
/// Short of it.
constexpr auto SHORT_PRESS = 400ms;

const QString ROBOT_NOTE = QStringLiteral("The robot paused twice");

QString id_of(const Entry& entry)
{
    return QString::fromStdString(entry.m_Id.format());
}

const ErrorEntry* error_of(const Entry& entry)
{
    return std::get_if<ErrorEntry>(&entry.m_Body);
}

/// The note on an error entry, or an empty string.
QString attached_note(const Entry& entry)
{
    const ErrorEntry* error = error_of(entry);
    return error != nullptr && error->m_AttachedNote ? QString::fromStdString(*error->m_AttachedNote) : QString();
}

/// The Edit entry sheet of the Log screen, null before the first hold has made it.
QQuickItem* edit_sheet(Screen& screen)
{
    return screen.rig.find_item(QStringLiteral("editSheet"));
}

bool edit_sheet_open(Screen& screen)
{
    QQuickItem* sheet = edit_sheet(screen);
    return sheet != nullptr && sheet->isVisible();
}

/// Types `text` into the composer, the way a person does.
void type_draft(Screen& screen, LogRig& sim, const QString& text)
{
    QMetaObject::invokeMethod(screen.named(QStringLiteral("logComposer")), "focusField");
    screen.rig.type(text);
    REQUIRE(sim.session.draft() == text);
}

/// Presses the tile and keeps the finger down for `HOLD`, which records the entry; lets the commit through (and what
/// waits for it); then lifts the finger.
void hold_and_commit(Screen& screen, LogRig& sim, int row)
{
    const int finger = screen.rig.press(screen.tile(row));
    screen.rig.advance(HOLD);
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.release(finger);
}

}  // namespace

TEST_CASE("longpress.opens_note: a hold beyond 500 ms on an empty composer opens the Edit sheet on the new entry, with the "
          "cursor in the note; a shorter press opens nothing",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    // A press shorter than the hold is a tap, and a tap logs nothing: no entry, no sheet.
    screen.rig.hold(screen.tile(MUTE_DELAY_ROW), SHORT_PRESS);
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(600ms);
    CHECK(sim.entries().empty());
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK_FALSE(edit_sheet_open(screen));
    CHECK(edit_sheet(screen) == nullptr);

    // A hold: the hold records the entry, and the sheet is open on it.
    const int finger = screen.rig.press(screen.tile(MUTE_DELAY_ROW));
    screen.rig.advance(400ms);
    CHECK(sim.gateway.pending() == 0); // not yet: the finger has been down for 400 ms
    CHECK_FALSE(edit_sheet_open(screen));
    screen.rig.advance(200ms);
    CHECK(sim.gateway.pending() == 1); // the hold asked for the entry
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.release(finger);
    screen.rig.advance(500ms);

    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(edit_sheet_open(screen));
    QQuickItem* sheet = edit_sheet(screen);
    CHECK(sheet->property("entryId").toString() == id_of(entries.back()));
    CHECK(sheet->property("kind").toString() == QStringLiteral("error"));
    QQuickItem* note = screen.inside(sheet, QStringLiteral("editNote"));
    CHECK(note->hasActiveFocus());
    CHECK(note->property("text").toString().isEmpty());
    // The entry is as the tap made it.
    CHECK(attached_note(entries.back()).isEmpty());
}

TEST_CASE("longpress.opens_note: Other, an empty position and a finger that slides off are not holds", "[ui][log][longpress]")
{
    Configuration configuration = sample_configuration();
    configuration.m_ErrorTypes.resize(9);
    LogRig sim(Options().existing().configuration(std::move(configuration)));
    sim.load();
    Screen screen(sim);
    QSignalSpy other(screen.item, SIGNAL(otherRequested()));

    screen.rig.hold(screen.tile(OTHER_ROW), HOLD);
    CHECK(other.count() == 1); // its tap, as before
    CHECK(sim.entries().empty());
    CHECK_FALSE(edit_sheet_open(screen));

    // An empty position (a nine-type board has two): it asks for the board setup, and nothing is held.
    QSignalSpy setup(screen.item, SIGNAL(boardSetupRequested(int)));
    screen.rig.hold(screen.tile(9), HOLD);
    CHECK(setup.count() == 1);
    CHECK(sim.entries().empty());

    // A finger that slides off the tile before 500 ms has gone: no sheet when the time comes.
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);
    const int finger = screen.rig.press(tile);
    screen.rig.advance(200ms);
    screen.rig.move(finger, tile, QPointF(tile->width() + 60, tile->height() / 2));
        screen.rig.advance(HOLD);
    screen.rig.release(finger);
    screen.rig.advance(500ms);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().empty());
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK_FALSE(edit_sheet_open(screen));
}

TEST_CASE("longpress.records_once: a hold records one entry, not two, with one haptic, on both paths",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    // Empty composer: the sheet path.
    hold_and_commit(screen, sim, MUTE_DELAY_ROW);
    screen.rig.advance(500ms);
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);
    CHECK(sim.timeline.entry_count() == 1);
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("1"));
    CHECK(edit_sheet_open(screen));

    // Cancel, then the draft path on the same tile: a second entry, and the count says two, not three.
    screen.rig.tap(screen.inside(edit_sheet(screen), QStringLiteral("navLeading")));
    screen.rig.advance(1000ms);
    type_draft(screen, sim, ROBOT_NOTE);
    hold_and_commit(screen, sim, MUTE_DELAY_ROW);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 2);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 2);
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("2"));
    CHECK(sim.feedback.recorded().size() == 2);
}

TEST_CASE("longpress.save: Save attaches the note to the entry, and it is on the same line in the timeline",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    hold_and_commit(screen, sim, MUTE_DELAY_ROW);
    screen.rig.advance(500ms);
    REQUIRE(edit_sheet_open(screen));
    QQuickItem* sheet = edit_sheet(screen);

    screen.inside(sheet, QStringLiteral("editNote"))->setProperty("text", ROBOT_NOTE);
    screen.rig.advance(50ms);
    screen.rig.tap(screen.inside(sheet, QStringLiteral("navTrailing")));
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(500ms);

    CHECK_FALSE(edit_sheet_open(screen));
    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()) == ROBOT_NOTE);
    // One line: the error and its note, as the shared log has it, and not a second line.
    CHECK(sim.timeline.entry_count() == 1);
    CHECK(screen.last_line().contains(QStringLiteral("Mute Delay")));
    CHECK(screen.last_line().contains(ROBOT_NOTE));
    CHECK(screen.text_of(QStringLiteral("lastBarCount")) == QStringLiteral("1 today"));
}

TEST_CASE("longpress.cancel: Cancel closes the sheet and leaves the entry as the tap made it", "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    hold_and_commit(screen, sim, MUTE_DELAY_ROW);
    screen.rig.advance(500ms);
    REQUIRE(edit_sheet_open(screen));
    QQuickItem* sheet = edit_sheet(screen);
    screen.inside(sheet, QStringLiteral("editNote"))->setProperty("text", ROBOT_NOTE);
    screen.rig.advance(50ms);
    sim.events.clear();

    screen.rig.tap(screen.inside(sheet, QStringLiteral("navLeading")));
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(500ms);

    CHECK_FALSE(edit_sheet_open(screen));
    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()).isEmpty());
    CHECK(sim.events.count(EventKind::COMMIT) == 0);
}

TEST_CASE("longpress.slow_commit: the hold records the entry, and the sheet waits for entryRecorded, then opens",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    const int finger = screen.rig.press(screen.tile(MUTE_DELAY_ROW));
    screen.rig.advance(HOLD);
    screen.rig.release(finger);
    screen.rig.advance(500ms);
    // The hold is over and nothing has been committed (the hold asked for the entry, once): no entry, no sheet.
    CHECK(sim.gateway.pending() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.entries().empty());
    CHECK_FALSE(edit_sheet_open(screen));

    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(500ms);
    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    REQUIRE(edit_sheet_open(screen));
    CHECK(edit_sheet(screen)->property("entryId").toString() == id_of(entries.front()));
    CHECK(screen.inside(edit_sheet(screen), QStringLiteral("editNote"))->hasActiveFocus());
}

TEST_CASE("longpress.voiceover: no gesture is needed: activating a tile records once and opens nothing, and Edit is the path",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);
    REQUIRE(accessible_press(tile));
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(HOLD);
    CHECK(sim.entries().size() == 1);
    CHECK_FALSE(edit_sheet_open(screen));

    // With no draft the tile has no "with note" element: the Edit sheet of Review is the way to a note.
    for (const Node& node : screen.rig.accessible_tree())
    {
        CAPTURE(node.m_Name.toStdString());
        CHECK_FALSE(node.m_Name.endsWith(QStringLiteral(" with note")));
    }
}

TEST_CASE("longpress.draft_attaches: a hold with a draft attaches it to the same line, records once and clears the draft",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* input = screen.named(QStringLiteral("noteInput"));
    type_draft(screen, sim, ROBOT_NOTE);

    // Down, 500 ms: the hold records the entry and the draft is attached to it, and the finger is still on the tile.
    const int finger = screen.rig.press(screen.tile(MUTE_DELAY_ROW));
    screen.rig.advance(400ms);
    CHECK(sim.events.count(EventKind::HAPTIC) == 0); // a press alone has recorded nothing and has not touched the draft
    CHECK(sim.session.draft() == ROBOT_NOTE);
    screen.rig.advance(200ms);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1); // the hold asked for the entry
    CHECK(sim.entries().empty());
    CHECK(sim.session.draft() == ROBOT_NOTE); // and the draft stays until the attach has committed
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.release(finger);

    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()) == ROBOT_NOTE);
    CHECK(sim.session.draft().isEmpty());
    CHECK(input->property("text").toString().isEmpty());
    CHECK_FALSE(edit_sheet_open(screen));
    // One entry and one line: the error with its note.
    CHECK(sim.timeline.entry_count() == 1);
    CHECK(screen.last_line().contains(ROBOT_NOTE));
    CHECK(screen.text_of(QStringLiteral("lastBarCount")) == QStringLiteral("1 today"));
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("1"));
    CHECK(screen.toast_text() == QStringLiteral("Mute Delay logged at 08:55 with note"));
    // One impact for the touch, and the note-saved confirmation after the commit (the sink's, as for a note).
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.feedback.recorded().size() == 1);
    REQUIRE(sim.feedback.saved_notes().size() == 1);
    CHECK(sim.feedback.saved_notes().front() == entries.front().m_Id);
}

TEST_CASE("longpress.draft_undo: one Undo removes the entry with its note and puts the draft back", "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* input = screen.named(QStringLiteral("noteInput"));
    type_draft(screen, sim, ROBOT_NOTE);
    hold_and_commit(screen, sim, MUTE_DELAY_ROW);
    sim.gateway.run_all();
    screen.rig.settle();
    REQUIRE(sim.entries().size() == 1);
    REQUIRE(sim.session.draft().isEmpty());
    REQUIRE(screen.toast_shown());

    screen.rig.tap(screen.named(QStringLiteral("toastUndo")));
    sim.gateway.run_all();
    screen.rig.settle();

    // Gone, note and all: nothing is left of the entry and nothing of a note entry either.
    CHECK(sim.entries().empty());
    CHECK(sim.timeline.entry_count() == 0);
    CHECK_FALSE(screen.badge_visible(MUTE_DELAY_ROW));
    CHECK_FALSE(screen.toast_shown());
    // The draft is back in the composer, with the cursor at its end.
    CHECK(sim.session.draft() == ROBOT_NOTE);
    CHECK(input->property("text").toString() == ROBOT_NOTE);
    CHECK(input->property("cursorPosition").toInt() == input->property("length").toInt());
    // One undo was enough, and a second has nothing to undo.
    CHECK_FALSE(sim.undo.undo());
}

TEST_CASE("longpress.draft_tap: a double-tap with a draft leaves the draft alone and logs the error alone",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    type_draft(screen, sim, ROBOT_NOTE);

    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    screen.rig.advance(600ms);

    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()).isEmpty());
    CHECK(sim.session.draft() == ROBOT_NOTE);
    CHECK(screen.named(QStringLiteral("noteInput"))->property("text").toString() == ROBOT_NOTE);
    CHECK(screen.toast_text() == QStringLiteral("Mute Delay logged at 08:55"));
    CHECK_FALSE(edit_sheet_open(screen));
    CHECK(sim.feedback.saved_notes().empty());

    // Undo of a plain tap does not touch the draft either.
    screen.rig.tap(screen.named(QStringLiteral("toastUndo")));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().empty());
    CHECK(sim.session.draft() == ROBOT_NOTE);
}

TEST_CASE("longpress.draft_slow_commit: a hold with a draft that passes before the commit attaches it once the entry is in",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    type_draft(screen, sim, ROBOT_NOTE);

    const int finger = screen.rig.press(screen.tile(MUTE_DELAY_ROW));
    screen.rig.advance(HOLD);
    screen.rig.release(finger);
    CHECK(sim.entries().empty());
    CHECK(sim.session.draft() == ROBOT_NOTE);

    sim.gateway.run_all(); // the entry; the attach is queued behind it
    screen.rig.settle();
    sim.gateway.run_all(); // the attach
    screen.rig.settle();

    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()) == ROBOT_NOTE);
    CHECK(sim.session.draft().isEmpty());
    CHECK_FALSE(edit_sheet_open(screen));
}

TEST_CASE("longpress.draft_failed: when the attach cannot be saved the entry stays, the draft stays, and the banner says so",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    type_draft(screen, sim, ROBOT_NOTE);

    const int finger = screen.rig.press(screen.tile(MUTE_DELAY_ROW));
    // The edit is the write to fail; the entry before it is saved.
    sim.storage.repository().fail_next(RepositoryOp::EDIT_ENTRY);
    screen.rig.advance(HOLD);
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.release(finger);

    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()).isEmpty());
    CHECK(sim.session.draft() == ROBOT_NOTE);
    CHECK(sim.events.count(EventKind::WRITE_FAILED, "attachDraft") == 1);
    CHECK(screen.named(QStringLiteral("logWriteFailedBanner"))->isVisible());
    CHECK(sim.feedback.saved_notes().empty());
    // The toast is the plain log's, and says so.
    CHECK(screen.toast_text() == QStringLiteral("Mute Delay logged at 08:55"));
}

TEST_CASE("longpress.note_action: with a draft each tile has a VoiceOver element 'Log <name> with note' that logs with it; "
          "without a draft there is none",
          "[ui][log][longpress]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    const auto with_note_names = [&screen]
    {
        QStringList names;
        for (const Node& node : screen.rig.accessible_tree())
        {
            if (node.m_Name.startsWith(QStringLiteral("Log ")) && node.m_Name.endsWith(QStringLiteral(" with note")))
            {
                names.push_back(node.m_Name);
            }
        }
        return names;
    };
    CHECK(with_note_names().isEmpty());
    // Made only while there is a draft, so a board with none pays nothing for them.
    CHECK(screen.rig.find_items(QStringLiteral("boardNoteAction")).empty());

    type_draft(screen, sim, ROBOT_NOTE);
    screen.rig.advance(32ms);
    // One for each tile that can be held: eleven error tiles, not Other.
    const QStringList names = with_note_names();
    CHECK(names.size() == 11);
    // A delegate for each row, over its tile; only the eleven that can be held are offered.
    CHECK(screen.rig.find_items(QStringLiteral("boardNoteAction")).size() == static_cast<std::size_t>(ROWS));
    CHECK(names.contains(QStringLiteral("Log Mute Delay with note")));
    CHECK_FALSE(names.contains(QStringLiteral("Log Other with note")));

    // Spaces alone are no draft.
    sim.session.set_draft(QStringLiteral("   "));
    screen.rig.advance(32ms);
    CHECK(with_note_names().isEmpty());
    sim.session.set_draft(ROBOT_NOTE);
    screen.rig.advance(32ms);

    // VoiceOver activates it: no gesture, one entry, the draft on its line.
    const std::vector<QQuickItem*> actions = screen.rig.find_items(QStringLiteral("boardNoteAction"));
    REQUIRE(actions.size() == static_cast<std::size_t>(ROWS));
    REQUIRE(accessible_press(actions[MUTE_DELAY_ROW]));
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    sim.gateway.run_all(); // the entry and the attach behind it
    screen.rig.settle();
    sim.gateway.run_all();
    screen.rig.settle();
    const std::vector<Entry> entries = sim.entries();
    REQUIRE(entries.size() == 1);
    CHECK(attached_note(entries.front()) == ROBOT_NOTE);
    CHECK(sim.session.draft().isEmpty());
    screen.rig.advance(32ms);
    CHECK(with_note_names().isEmpty());
    CHECK(screen.rig.find_items(QStringLiteral("boardNoteAction")).empty());
    CHECK(screen.toast_text() == QStringLiteral("Mute Delay logged at 08:55 with note"));
}

// ------------------------------------------------------------------------------------------------- the double-tap

TEST_CASE(
    "doubletap.logs: one tap logs nothing, the second tap logs one entry with the haptic and the press at its touch",
    "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);
    QObject* pressing = screen.child(tile, QStringLiteral("tilePressIn"));

    // The first tap: no haptic, no press animation, nothing asked of the session.
    const int first = screen.rig.press(tile);
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.gateway.pending() == 0);
    CHECK_FALSE(pressing->property("running").toBool());
    screen.rig.release(first);
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK(sim.gateway.pending() == 0);

    // The second tap, inside the window: the haptic and the press animation are at its touch, and the entry is asked
    // for.
    screen.rig.advance(DOUBLE_TAP_GAP);
    const int second = screen.rig.press(tile);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.events.count(EventKind::COMMIT) == 0);
    CHECK(sim.gateway.pending() == 1);
    CHECK(pressing->property("running").toBool());
    CHECK_FALSE(screen.toast_shown());
    screen.rig.release(second);
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(WINDOW_RUN_OUT);

    // One entry, one haptic, the toast as it was, and no hint.
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);
    CHECK(screen.badge_text(MUTE_DELAY_ROW) == QStringLiteral("1"));
    CHECK(screen.toast_text() == QStringLiteral("Mute Delay logged at 08:55"));
    CHECK_FALSE(screen.hint_shown());
    CHECK(screen.hint() == nullptr);

    // Another double-tap while the toast shows is "x2" on it.
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    CHECK(sim.entries().size() == 2);
    CHECK(screen.toast_text().contains(times(2)));
}

TEST_CASE("doubletap.window: two taps 250 ms apart log, two taps 400 ms apart are two single taps",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);

    screen.rig.tap(tile);
    screen.rig.advance(250ms);
    screen.rig.tap(tile);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
    screen.rig.advance(WINDOW_RUN_OUT);

    QQuickItem* other = screen.tile(0);
    screen.rig.tap(other);
    screen.rig.advance(400ms);
    screen.rig.tap(other);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);

    // A tap on another tile in between is not the first half of anything.
    screen.rig.advance(WINDOW_RUN_OUT);
    screen.rig.tap(screen.tile(1));
    screen.rig.advance(DOUBLE_TAP_GAP);
    screen.rig.tap(screen.tile(2));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
}

TEST_CASE("doubletap.triple: a triple-tap logs once, and the fourth tap starts a new double-tap",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);

    const auto tap_after_gap = [&]
    {
        screen.rig.tap(tile);
        screen.rig.advance(DOUBLE_TAP_GAP);
    };
    tap_after_gap(); // 1
    tap_after_gap(); // 2: logs
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);

    tap_after_gap(); // 3: the same gesture, logs nothing more
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);

    tap_after_gap(); // 4: the first of a new double-tap
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);

    tap_after_gap(); // 5: its second
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 2);
    CHECK(sim.events.count(EventKind::HAPTIC) == 2);

    // The taps of a gesture that logged send no hint.
    screen.rig.advance(WINDOW_RUN_OUT);
    CHECK(screen.hint() == nullptr);
}

TEST_CASE("doubletap.slide_off: a first tap that slides off is no tap, and a second that slides off has logged",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);
    const QPointF outside(tile->width() + 60, tile->height() / 2);

    // The slid-off touch is nothing, so the tap after it is a first tap, and there is no hint for the lost one.
    const int lost = screen.rig.press(tile);
    screen.rig.move(lost, tile, outside);
    screen.rig.release(lost);
    screen.rig.advance(DOUBLE_TAP_GAP);
    screen.rig.tap(tile);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().empty());
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);

    // A tap, then a touch that slides off: the second touch has asked, once, as a finger that slides off always has.
    screen.rig.advance(WINDOW_RUN_OUT);
    screen.rig.tap(tile);
    screen.rig.advance(DOUBLE_TAP_GAP);
    const int second = screen.rig.press(tile);
    screen.rig.move(second, tile, outside);
    screen.rig.release(second);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC) == 1);
}

TEST_CASE("doubletap.hint: one tap shows 'Double-tap to log' after the window, and not again for a few seconds",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);
    CHECK(screen.hint() == nullptr); // a board nobody taps wrongly pays nothing for it

    screen.rig.tap(tile);
    screen.rig.advance(100ms);
    CHECK_FALSE(screen.hint_shown()); // the second tap may still come
    screen.rig.advance(300ms);
    REQUIRE(screen.hint_shown());
    QQuickItem* hint = screen.hint();
    CHECK(hint->property("text").toString() == QStringLiteral("Double-tap to log"));
    CHECK(hint->property("shownCount").toInt() == 1);
    // It logged nothing and fired nothing, and the toast did not open.
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().empty());
    CHECK(sim.events.count(EventKind::HAPTIC) == 0);
    CHECK_FALSE(screen.toast_shown());
    CHECK_FALSE(screen.badge_visible(MUTE_DELAY_ROW));

    // It takes no touch: nothing in it is a control or a pointer handler, and it is named for a screen reader.
    for (const Node& node : screen.rig.tree(hint))
    {
        CAPTURE(node.m_ObjectName.toStdString());
        CHECK_FALSE(node.m_Interactive);
    }
    CHECK(hint->property("active").toBool());

    // It goes by itself (2 s and a fade), and a tap inside the interval (5 s from its start) shows nothing.
    screen.rig.advance(2500ms);
    CHECK_FALSE(screen.hint_shown());
    screen.rig.tap(tile);
    screen.rig.advance(WINDOW_RUN_OUT);
    CHECK_FALSE(screen.hint_shown());
    CHECK(hint->property("shownCount").toInt() == 1);

    // Once the interval has passed the next single tap shows it again.
    screen.rig.advance(3000ms);
    screen.rig.tap(tile);
    screen.rig.advance(WINDOW_RUN_OUT);
    CHECK(screen.hint_shown());
    CHECK(hint->property("shownCount").toInt() == 2);
    CHECK(sim.entries().empty());
}

TEST_CASE("doubletap.hint: it stands above the undo toast and never covers it", "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    screen.double_tap_tile(sim, MUTE_DELAY_ROW);
    REQUIRE(screen.toast_shown());

    screen.rig.tap(screen.tile(0));
    screen.rig.advance(WINDOW_RUN_OUT);
    REQUIRE(screen.hint_shown());
    screen.rig.advance(300ms);
    const QRectF toast = screen.rig.geometry(screen.named(QStringLiteral("toastCard")));
    const QRectF card = screen.rig.geometry(screen.inside(screen.hint(), QStringLiteral("tapHintCard")));
    CHECK(card.bottom() <= toast.top());
    CHECK(card.width() == toast.width());
    // Still inside the screen, over the board and not over the header.
    CHECK(card.top() > screen.rig.geometry(screen.named(QStringLiteral("logLastBar"))).bottom());
}

TEST_CASE("doubletap.hold: a tap and then a hold records once, at the second touch, and the hold adds the note",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);

    screen.rig.tap(tile);
    screen.rig.advance(DOUBLE_TAP_GAP);
    const int finger = screen.rig.press(tile);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.gateway.pending() == 1);
    screen.rig.advance(HOLD);
    // The hold did not ask a second time.
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(sim.gateway.pending() == 1);
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.release(finger);
    screen.rig.advance(500ms);

    REQUIRE(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(edit_sheet_open(screen));
    CHECK(edit_sheet(screen)->property("entryId").toString() == id_of(sim.entries().front()));
}

TEST_CASE("doubletap.hold_then_tap: a hold is not a tap, so the tap after it starts a new gesture",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    QQuickItem* tile = screen.tile(MUTE_DELAY_ROW);

    hold_and_commit(screen, sim, MUTE_DELAY_ROW);
    screen.rig.advance(500ms);
    REQUIRE(sim.entries().size() == 1);
    // Close the sheet that the hold opened.
    screen.rig.tap(screen.inside(edit_sheet(screen), QStringLiteral("navLeading")));
    screen.rig.advance(1000ms);

    screen.rig.tap(tile);
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 1);
}

TEST_CASE("doubletap.voiceover: one activation logs once, as the double-tap of VoiceOver itself, with no hint",
          "[ui][log][doubletap]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);

    REQUIRE(accessible_press(screen.tile(MUTE_DELAY_ROW)));
    sim.gateway.run_all();
    screen.rig.settle();
    screen.rig.advance(WINDOW_RUN_OUT);
    CHECK(sim.entries().size() == 1);
    CHECK(sim.events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(screen.hint() == nullptr);

    // A second activation is a second entry, as it always was.
    REQUIRE(accessible_press(screen.tile(MUTE_DELAY_ROW)));
    sim.gateway.run_all();
    screen.rig.settle();
    CHECK(sim.entries().size() == 2);
}

namespace
{

QJsonObject budget()
{
    QFile file(QStringLiteral(EJ_PERF_BUDGET));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object().value(QStringLiteral("scene")).toObject().value(QStringLiteral("LogScreen")).toObject();
}

constexpr double PRESS_BUDGET_MS = 16.0;
constexpr int PRESSES = 200;

}  // namespace

TEST_CASE(
    "perf.press_path: from the second tap's touch to the visual feedback, p95 over 200 double-taps is under 16 ms, the "
    "haptic first and nothing committed",
    "[ui][perf]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    std::vector<QQuickItem*> tiles;
    // The toast covers the foot of the board and takes its touches, so the presses go to the first three rows.
    for (int row = 0; row < COVERED_BY_TOAST_FROM; ++row)
    {
        tiles.push_back(screen.tile(row));
    }

    int fed_back = 0;
    int haptic_first = 0;
    std::vector<double> times;
    times.reserve(PRESSES);
    for (int i = 0; i < PRESSES; ++i)
    {
        QQuickItem* tile = tiles[static_cast<std::size_t>(i) % tiles.size()];
        QObject* pressing = tile->findChild<QObject*>(QStringLiteral("tilePressIn"));
        REQUIRE(pressing != nullptr);
        // The first tap is not timed and logs nothing. Its window is made short and run out, so that a tile that comes
        // round again starts a new double-tap.
        tile->setProperty("doubleTapMs", 20);
        screen.rig.tap(tile);
        const std::size_t haptics_before = sim.events.count(EventKind::HAPTIC);
        // The budget (NFR-03, BRD-03) is counted from the touch of the second tap.
        const auto start = std::chrono::steady_clock::now();
        const int finger = screen.rig.press(tile);
        // What the finger feels and sees: the haptic has fired, the entry has been asked for, and the press animation
        // runs.
        const bool haptic = sim.events.count(EventKind::HAPTIC) == haptics_before + 1;
        const bool submitted = sim.gateway.pending() >= 1;
        const bool animating = pressing->property("running").toBool() || tile->property("down").toBool();
        const auto end = std::chrono::steady_clock::now();
        screen.rig.release(finger);
        fed_back += (haptic && submitted && animating) ? 1 : 0;
        // The haptic is the first thing in the log for this press, and nothing has been committed yet.
        const auto events = sim.events.events();
        if (!events.empty() && events.back().m_Kind == EventKind::HAPTIC && sim.events.count(EventKind::COMMIT) == 0)
        {
            ++haptic_first;
        }
        times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
        // The commit and the count, and the window of the gesture, outside the timed part.
        sim.gateway.run_all();
        screen.rig.settle();
        screen.rig.advance(64ms);
        sim.events.clear();
    }
    std::sort(times.begin(), times.end());

    CHECK(fed_back == PRESSES);
    CHECK(haptic_first == PRESSES);
    CHECK(sim.entries().size() == static_cast<std::size_t>(PRESSES));
    const double p95 = percentile(times, 0.95);
    INFO("p95 " << p95 << " ms, median " << percentile(times, 0.5) << " ms, worst " << times.back() << " ms");
    CHECK(p95 < PRESS_BUDGET_MS * perf_budget_factor());
}

TEST_CASE("perf.scene: the items and the delegates of the Log screen stay under the budget in perf-budget.json",
          "[ui][perf]")
{
    LogRig sim(Options().existing());
    sim.load();
    Screen screen(sim);
    const SceneSize size = measure_scene(screen.rig);
    const QJsonObject limit = budget();
    REQUIRE_FALSE(limit.isEmpty());
    INFO("items " << size.m_Items << ", delegates " << size.m_Delegates);
    // The delegates are the board's 12 tiles and the composer's template chips, one for each template of the sample
    // configuration (two): 14. The rig counts what every Repeater has made, so the chips are in the number; the budget
    // is what the screen needs, and a Repeater or a Loader for each tile would raise it.
    constexpr int TILE_DELEGATES = 12;
    constexpr int CHIP_DELEGATES = 2;
    CHECK(limit.value(QStringLiteral("delegates")).toInt() == TILE_DELEGATES + CHIP_DELEGATES);
    CHECK(size.m_Delegates == TILE_DELEGATES + CHIP_DELEGATES);
    CHECK(size.m_Items <= limit.value(QStringLiteral("items")).toInt());
}

TEST_CASE("perf.first_frame: the loading screen draws all 12 tile rectangles in its first frame, with no text",
          "[ui][perf]")
{
    LogRig sim(Options().existing());
    Screen screen(sim);
    REQUIRE(sim.session.state() == LogSession::State::LOADING);

    int rectangles = 0;
    for (int row = 0; row < ROWS; ++row)
    {
        QQuickItem* background = screen.inside(screen.tile(row), QStringLiteral("tileBackground"));
        if (background != nullptr && background->isVisible() && background->width() > 0 && background->height() > 0)
        {
            ++rectangles;
        }
    }
    CHECK(rectangles == ROWS);

    // No text is shown in any part of the screen.
    for (const Node& node : screen.rig.tree())
    {
        if (node.m_TypeName.contains(QStringLiteral("QQuickText")) && node.m_Visible)
        {
            CAPTURE(node.m_ObjectName);
            CHECK(node.m_Item->property("text").toString().isEmpty());
        }
    }
}

#include "log_screen_test.moc"
