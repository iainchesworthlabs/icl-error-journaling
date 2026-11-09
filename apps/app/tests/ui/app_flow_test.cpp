// SPDX-License-Identifier: GPL-3.0-or-later
// The app shell (docs/plan/stage-4.md, 4.15): the whole app, driven through the screens. The services are the ones
// `app::AppRoot` builds for the phone, over the in-memory store and the fake clock and platform (Stage 3's `Rig`), and
// the shell is the item that `Main.qml` holds, so what is tested here is what runs on the phone short of the window
// itself. The bridge is a small QObject with the names of the QmlBridge; its haptics go into the same EventLog as the
// commits, so the order of the feedback is the order of the log. The file chooser is a stub: a real dialog waits for a
// person.
//
// Cases: app.no_services_missing, app.first_frame, app.first_launch, app.import, app.navigation, app.sheets,
// app.new_day, app.history, app.flow, app.settings, app.export, app.messages, app.theme, app.hooks, app.timing,
// app.accessibility.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort them the other way, so it is switched off.
// clang-format off
#include "support/rig.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/day_watcher.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/undo_controller.hpp"

#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemModel>
#include <QByteArray>
#include <QColor>
#include <QFile>
#include <QHash>
#include <QMetaObject>
#include <QObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QSize>
#include <QSizeF>
#include <QString>
#include <QVariant>
#include <QVariantMap>
#include <chrono>
#include <cstdio>
#include <memory>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

constexpr auto MODULE = "ErrorJournalingApp";
constexpr int LOG_TAB = 0;
constexpr int REVIEW_TAB = 1;
constexpr int HISTORY_TAB = 2;
constexpr int SETTINGS_TAB = 3;

/// The part of the bridge the shell and the screens call, with the names of `QmlBridge`: the haptics go through the
/// rig's `FakePlatform`, which writes them into the event log; the accessibility settings are properties a test
/// changes.
class FakeBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double textScale READ text_scale WRITE set_text_scale NOTIFY textScaleChanged)
    Q_PROPERTY(bool reduceMotion READ reduce_motion WRITE set_reduce_motion NOTIFY reduceMotionChanged)
    Q_PROPERTY(QString timingSummary READ timing_summary NOTIFY timingChanged)

public:
    explicit FakeBridge(FakePlatform& platform) : m_Platform(platform) {}

    [[nodiscard]] double text_scale() const
    {
        return m_TextScale;
    }
    void set_text_scale(double scale)
    {
        m_TextScale = scale;
        emit textScaleChanged();
    }
    [[nodiscard]] bool reduce_motion() const
    {
        return m_ReduceMotion;
    }
    void set_reduce_motion(bool reduce)
    {
        m_ReduceMotion = reduce;
        emit reduceMotionChanged();
    }

    [[nodiscard]] QString timing_summary() const
    {
        return QStringLiteral("launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms");
    }

    Q_INVOKABLE void prepareHaptics()
    {
        m_Platform.prepare();
    }
    Q_INVOKABLE void setBoardVisible(bool visible)
    {
        m_Visible.push_back(visible);
    }
    Q_INVOKABLE void setApplicationActive(bool active)
    {
        m_Active.push_back(active);
    }
    Q_INVOKABLE void hapticImpact(double /*pressTimestamp*/ = 0)
    {
        m_Platform.fire(FakePlatform::haptic_kind::IMPACT);
    }
    Q_INVOKABLE void hapticSuccess()
    {
        m_Platform.fire(FakePlatform::haptic_kind::SUCCESS);
    }

    std::vector<bool> m_Visible;
    std::vector<bool> m_Active;

signals:
    void textScaleChanged();
    void reduceMotionChanged();
    void timingChanged();

private:
    FakePlatform& m_Platform;
    double m_TextScale = 1.0;
    bool m_ReduceMotion = false;
};

QVariant role_of(const QAbstractItemModel& model, int row, const char* role)
{
    const QHash<int, QByteArray> names = model.roleNames();
    for (auto it = names.begin(); it != names.end(); ++it)
    {
        if (it.value() == role)
        {
            return model.data(model.index(row, 0), it.key());
        }
    }
    return {};
}

int row_with_label(const QAbstractItemModel& model, const QString& label, const char* role = "label")
{
    for (int row = 0; row < model.rowCount(); ++row)
    {
        if (role_of(model, row, role).toString() == label)
        {
            return row;
        }
    }
    return -1;
}

/// The application as the phone has it: the services over the fakes, the shell on top, and a file chooser that shows no
/// dialog. `configured` false is a first launch.
struct App
{
    explicit App(bool configured = true, bool load_shell = true)
        : services(RigStore::MEMORY), bridge(services.platform())
    {
        if (configured)
        {
            services.import_sample_configuration();
        }
        ui.on_advance([this](std::chrono::milliseconds by) { services.advance(by); });
        ui.on_settle([this] { services.settle(); });
        QQmlComponent component(&ui.engine(), MODULE, "UiStubFileChooser");
        chooser.reset(component.create());
        REQUIRE(chooser != nullptr);
        if (load_shell)
        {
            load();
        }
    }

    App(const App&) = delete;
    App& operator=(const App&) = delete;

    [[nodiscard]] QVariantMap properties()
    {
        const auto object = [](QObject& value) { return QVariant::fromValue<QObject*>(&value); };
        auto& root = services.root();
        return QVariantMap{{QStringLiteral("session"), object(*root.session())},
                           {QStringLiteral("undo"), object(*root.undo())},
                           {QStringLiteral("board"), object(*root.board())},
                           {QStringLiteral("picker"), object(*root.picker())},
                           {QStringLiteral("timeline"), object(*root.timeline())},
                           {QStringLiteral("history"), object(*root.history())},
                           {QStringLiteral("day"), object(*root.day())},
                           {QStringLiteral("settings"), object(*root.settings())},
                           {QStringLiteral("share"), object(*root.share())},
                           {QStringLiteral("appRoot"), object(root)},
                           {QStringLiteral("bridge"), object(bridge)},
                           {QStringLiteral("fileChooser"), object(*chooser)},
                           {QStringLiteral("aboutText"), QStringLiteral("0.1.0 · test, storage: memory")}};
    }

    void load(const QVariantMap& without = {})
    {
        QVariantMap given = properties();
        for (auto it = without.begin(); it != without.end(); ++it)
        {
            given.remove(it.key());
        }
        shell = ui.load(QStringLiteral("AppShell"), given);
        INFO(ui.errors().toStdString());
        REQUIRE(shell != nullptr);
        ui.settle();
        ui.advance(50ms);
    }

    /// The first item with this objectName under the shell, which must be there.
    [[nodiscard]] QQuickItem* need(const QString& name)
    {
        QQuickItem* item = ui.find_item(name);
        INFO(name.toStdString());
        REQUIRE(item != nullptr);
        return item;
    }

    /// Whether the item exists and is shown, which for an item in a stack means that its page is the one in view.
    [[nodiscard]] bool shown(const QString& name)
    {
        QQuickItem* item = ui.find_item(name);
        return item != nullptr && item->isVisible() && item->width() > 0;
    }

    void tap(const QString& name)
    {
        ui.tap(need(name));
        ui.advance(16ms);
    }

    /// Lets the transition of a page or a sheet finish.
    void play()
    {
        ui.advance(500ms);
    }

    void choose_tab(int tab)
    {
        static const char* const NAMES[] = {"tabLog", "tabReview", "tabHistory", "tabSettings"};
        tap(QString::fromLatin1(NAMES[tab]));
        play();
    }

    [[nodiscard]] int current_tab()
    {
        return need(QStringLiteral("shellTabBar"))->property("currentIndex").toInt();
    }

    /// The tile of the board that shows this label.
    [[nodiscard]] QString tile(const QString& label)
    {
        const int row = row_with_label(*services.root().board(), label);
        REQUIRE(row >= 0);
        return QStringLiteral("tile%1").arg(row);
    }

    [[nodiscard]] int count_of(const QString& label)
    {
        const int row = row_with_label(*services.root().board(), label);
        return row < 0 ? -1 : role_of(*services.root().board(), row, "count").toInt();
    }

    [[nodiscard]] QObject& stub() const
    {
        return *chooser;
    }

    /// A file the person could choose, in the store the rig reads files from (the fake of the in-memory kind).
    void add_file(const std::string& path, const std::string& text)
    {
        services.memory().config().add_file(path, text);
    }

    /// The sample configuration of the repository, as the file a person is given.
    [[nodiscard]] static std::string sample_setup()
    {
        QFile file(QStringLiteral(EJ_SAMPLE_CONFIG));
        REQUIRE(file.open(QIODevice::ReadOnly));
        return file.readAll().toStdString();
    }

    /// Chooses a file in the stub chooser, as the person does in the dialog.
    void choose_to_open(const QString& path, const QString& name)
    {
        REQUIRE(QMetaObject::invokeMethod(chooser.get(), "openChosen", Q_ARG(QString, path), Q_ARG(QString, name)));
        ui.settle();
        play();
    }

    void choose_to_save(const QString& path)
    {
        REQUIRE(QMetaObject::invokeMethod(chooser.get(), "saveChosen", Q_ARG(QString, path)));
        ui.settle();
        play();
    }

    /// Fills the header that the header screen has its own tests for, so that the log has all five fields.
    void fill_header()
    {
        auto& session = services.session();
        REQUIRE(session.correctHeader(QStringLiteral("unit_number"), QStringLiteral("27")));
        auto& locations = *services.root().settings()->locations();
        const int row = row_with_label(locations, QStringLiteral("North Site"), "name");
        REQUIRE(row >= 0);
        REQUIRE(session.correctHeader(QStringLiteral("location"), role_of(locations, row, "locationId").toString()));
        REQUIRE(session.correctVersion(QStringLiteral("1.4.2"), QStringLiteral("a1b2c3d")));
        ui.settle();
    }

    Rig services;
    FakeBridge bridge;
    UiRig ui;
    std::unique_ptr<QObject> chooser;
    QQuickItem* shell = nullptr;
};

/// The objectName of the item's parent, empty when there is none.
QString parent_name(const QQuickItem* item)
{
    return item != nullptr && item->parentItem() != nullptr ? item->parentItem()->objectName() : QString();
}

/// The index in the event log of the first event of this kind after `from`; -1 when there is none.
int first_after(EventLog& log, EventKind kind, std::size_t from)
{
    const auto events = log.events();
    for (std::size_t i = from; i < events.size(); ++i)
    {
        if (events[i].m_Kind == kind)
        {
            return static_cast<int>(i);
        }
    }
    return -1;
}

} // namespace

TEST_CASE("app.no_services_missing: the shell is made with every required property, and fails loudly without one",
          "[ui][app]")
{
    {
        App app;
        CHECK(app.shell != nullptr);
    }
    // (A missing `picker` is left out: Qt's SortFilterProxyModel crashes on an undefined source while the Other picker
    // is being made, before the required property is reported. The property is declared required in the same way.)
    for (const char* missing :
         {"session", "undo", "board", "timeline", "history", "day", "settings", "share", "appRoot", "bridge"})
    {
        CAPTURE(missing);
        App app(true, false);
        QVariantMap given = app.properties();
        given.remove(QString::fromLatin1(missing));
        CHECK(app.ui.load(QStringLiteral("AppShell"), given) == nullptr);
        CHECK(app.ui.errors().contains(QString::fromLatin1(missing)));
    }
}

TEST_CASE("app.first_frame: the Log screen is the first page and nothing else has been built", "[ui][app]")
{
    App app;

    CHECK(app.shown(QStringLiteral("logScreen")));
    CHECK(app.shown(QStringLiteral("shellTabBar")));
    CHECK(app.ui.find_item(QStringLiteral("reviewScreen")) == nullptr);
    CHECK(app.ui.find_item(QStringLiteral("settingsScreen")) == nullptr);
    CHECK(app.ui.find_item(QStringLiteral("historyScreen")) == nullptr);
    CHECK(app.need(QStringLiteral("firstLaunch"))->childItems().isEmpty());
    // The haptic policy was told that the board is on screen.
    REQUIRE_FALSE(app.bridge.m_Visible.empty());
    CHECK(app.bridge.m_Visible.back());
}

TEST_CASE("app.first_launch: with no setup the first-launch screen covers the tabs until a setup file is imported",
          "[ui][app]")
{
    App app(false);
    REQUIRE_FALSE(app.services.root().settings()->configured());

    CHECK(app.shown(QStringLiteral("importCard")));
    CHECK(app.shown(QStringLiteral("manualCard")));
    CHECK_FALSE(app.shown(QStringLiteral("shellTabBar")));
    CHECK_FALSE(app.shown(QStringLiteral("logScreen")));

    // The import card asks the chooser for a file, once.
    app.tap(QStringLiteral("importCard"));
    CHECK(app.stub().property("opens").toInt() == 1);

    // The chosen file is read and the confirmation shows what it holds, over the first-launch screen.
    app.add_file("sample-config.json", App::sample_setup());
    app.choose_to_open(QStringLiteral("sample-config.json"), QStringLiteral("sample-config.json"));
    CHECK(app.shown(QStringLiteral("importConfirm")));
    CHECK_FALSE(app.services.root().settings()->configured());

    app.tap(QStringLiteral("replaceButton"));
    app.ui.settle();
    app.play();

    CHECK(app.services.root().settings()->configured());
    CHECK_FALSE(app.shown(QStringLiteral("importConfirm")));
    CHECK_FALSE(app.shown(QStringLiteral("importCard")));
    CHECK(app.shown(QStringLiteral("shellTabBar")));
    CHECK(app.shown(QStringLiteral("logScreen")));
    // The board shows the imported setup.
    CHECK(app.services.root().board()->rowCount() == 12);
    CHECK(row_with_label(*app.services.root().board(), QStringLiteral("Wrong Response")) >= 0);
}

TEST_CASE("app.import: a file that cannot be read shows the core's messages and \"Choose another file\" asks again",
          "[ui][app]")
{
    App app;
    app.add_file("broken.json", "{ this is not a setup file");
    app.choose_tab(SETTINGS_TAB);
    app.choose_to_open(QStringLiteral("broken.json"), QStringLiteral("broken.json"));
    CHECK(app.shown(QStringLiteral("importErrors")));
    CHECK_FALSE(app.shown(QStringLiteral("importConfirm")));
    // The setup is as it was.
    CHECK(app.services.root().board()->rowCount() == 12);

    REQUIRE(QMetaObject::invokeMethod(app.need(QStringLiteral("importErrors")), "chooseAnotherRequested"));
    app.play();
    CHECK_FALSE(app.shown(QStringLiteral("importErrors")));
    CHECK(app.stub().property("opens").toInt() == 1);
}

TEST_CASE("app.navigation: each tab shows its screen, a tab that is tapped again goes back to its first page, and the "
          "state of a tab is kept",
          "[ui][app]")
{
    App app;
    app.fill_header();
    CHECK(app.shown(QStringLiteral("logScreen")));

    app.choose_tab(REVIEW_TAB);
    CHECK(app.current_tab() == REVIEW_TAB);
    CHECK(app.shown(QStringLiteral("reviewScreen")));
    CHECK_FALSE(app.shown(QStringLiteral("logScreen")));
    // The Log screen told the haptic policy that the board went away.
    CHECK_FALSE(app.bridge.m_Visible.back());

    app.choose_tab(HISTORY_TAB);
    CHECK(app.shown(QStringLiteral("historyScreen")));
    CHECK_FALSE(app.shown(QStringLiteral("reviewScreen")));

    app.choose_tab(SETTINGS_TAB);
    CHECK(app.shown(QStringLiteral("settingsScreen")));
    CHECK(app.shown(QStringLiteral("settingsFacilitator")));

    // The Settings tab pushes the editors on its own stack, and a second tap on the tab pops them.
    app.tap(QStringLiteral("settingsErrorTypes"));
    app.play();
    CHECK(app.shown(QStringLiteral("addRow")));
    CHECK_FALSE(app.shown(QStringLiteral("settingsFacilitator")));
    CHECK(app.shown(QStringLiteral("shellTabBar")));
    app.choose_tab(SETTINGS_TAB);
    CHECK_FALSE(app.shown(QStringLiteral("addRow")));
    CHECK(app.shown(QStringLiteral("settingsFacilitator")));

    // The editors are pages too: the list rows of Locations and the note templates open the same way, and their own
    // "back" returns to Settings.
    app.tap(QStringLiteral("settingsLocations"));
    app.play();
    CHECK_FALSE(app.shown(QStringLiteral("settingsFacilitator")));
    REQUIRE(QMetaObject::invokeMethod(app.need(QStringLiteral("settingsScreen")), "popPage"));
    app.play();
    CHECK(app.shown(QStringLiteral("settingsFacilitator")));
    app.tap(QStringLiteral("settingsTemplates"));
    app.play();
    CHECK_FALSE(app.shown(QStringLiteral("settingsFacilitator")));
    app.choose_tab(SETTINGS_TAB);
    CHECK(app.shown(QStringLiteral("settingsFacilitator")));

    // Back on Log, the screen is where it was left.
    app.choose_tab(LOG_TAB);
    CHECK(app.shown(QStringLiteral("logScreen")));
    CHECK(app.bridge.m_Visible.back());
}

TEST_CASE("app.sheets: Today's details covers the tab bar and closes on \"Start logging\", Other opens the picker over "
          "the tab bar, and a row of Review opens Edit over it",
          "[ui][app]")
{
    App app;

    // The header card opens Today's details, which has no tab bar (the design draws none).
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    CHECK(app.shown(QStringLiteral("headerScreen")));
    CHECK_FALSE(app.shown(QStringLiteral("shellTabBar")));
    CHECK_FALSE(app.shown(QStringLiteral("logScreen")));
    CHECK_FALSE(app.bridge.m_Visible.back());
    app.tap(QStringLiteral("headerStart"));
    app.play();
    CHECK_FALSE(app.shown(QStringLiteral("headerScreen")));
    CHECK(app.shown(QStringLiteral("shellTabBar")));
    CHECK(app.shown(QStringLiteral("logScreen")));
    CHECK(app.bridge.m_Visible.back());

    // The Other tile opens the picker in the window's layer, which covers the tab bar, and records nothing.
    const int before = app.services.root().board()->rowCount();
    REQUIRE(before == 12);
    app.tap(QStringLiteral("tile11"));
    app.play();
    QQuickItem* picker = app.need(QStringLiteral("otherPicker"));
    CHECK(picker->property("isOpen").toBool());
    CHECK(parent_name(picker) == QStringLiteral("sheetHostLayer"));
    CHECK(app.services.session().live_entry_count() == 0);
    // While it is open the screen below is disabled.
    CHECK_FALSE(app.need(QStringLiteral("shellTabs"))->isEnabled());
    REQUIRE(QMetaObject::invokeMethod(picker, "close"));
    app.play();
    CHECK(app.need(QStringLiteral("shellTabs"))->isEnabled());

    // A row of Review opens Edit, a sheet of the window's layer.
    app.fill_header();
    REQUIRE(app.services.tap(role_of(*app.services.root().board(), 0, "typeId").toString()));
    app.ui.settle();
    app.choose_tab(REVIEW_TAB);
    QQuickItem* row = app.need(QStringLiteral("reviewSwipe"));
    app.ui.tap(row);
    app.play();
    QQuickItem* editor = app.need(QStringLiteral("editSheet"));
    CHECK(editor->property("isOpen").toBool());
    CHECK(parent_name(editor) == QStringLiteral("sheetHostLayer"));
    CHECK_FALSE(app.need(QStringLiteral("shellTabs"))->isEnabled());
    REQUIRE(QMetaObject::invokeMethod(editor, "close"));
    app.play();
    CHECK(app.need(QStringLiteral("shellTabs"))->isEnabled());
}

TEST_CASE("app.flow: log taps with the haptic first, a note, undo, Review, Edit, Share, Settings and back", "[ui][app]")
{
    App app;
    auto& events = app.services.events();
    auto& session = app.services.session();
    auto& undo = *app.services.root().undo();
    REQUIRE(session.state() == viewmodel::LogSession::State::OPEN);

    // Today's details, filled by the person with the header screen's own tests behind it, then closed.
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    app.fill_header();
    app.tap(QStringLiteral("headerStart"));
    app.play();
    CHECK(session.missing_fields().isEmpty());

    // A double-tap: the impact haptic fires at the touch of the second tap, before the entry is committed; the count
    // and the toast follow it. The first tap logs nothing.
    events.clear();
    app.services.go_to(7, 10, 5);
    const QString mute = app.tile(QStringLiteral("Mute Delay"));
    app.ui.tap(app.need(mute));
    CHECK(events.count(EventKind::HAPTIC, "impact") == 0);
    const int finger = app.ui.press(app.need(mute));
    // The haptic has fired and the entry has been asked for, and nothing has been committed.
    CHECK(events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(events.count(EventKind::COMMIT) == 0);
    app.ui.release(finger);
    app.ui.settle();
    const int haptic = static_cast<int>(events.first_index(EventKind::HAPTIC, "impact").value_or(-1));
    const int commit = first_after(events, EventKind::COMMIT, 0);
    REQUIRE(haptic >= 0);
    REQUIRE(commit >= 0);
    CHECK(haptic < commit);
    CHECK(events.count(EventKind::HAPTIC, "impact") == 1);
    CHECK(app.count_of(QStringLiteral("Mute Delay")) == 1);
    CHECK(undo.active());
    app.ui.advance(400ms); // the toast, and the window of the double-tap runs out
    CHECK(app.shown(QStringLiteral("logToast")));
    CHECK(app.need(QStringLiteral("logLastBar"))->isVisible());

    // A second double-tap on the tile is "x2" on the same toast, with one more haptic first.
    app.services.go_to(7, 10, 5, 1);
    app.ui.tap(app.need(mute));
    app.ui.tap(app.need(mute));
    app.ui.settle();
    CHECK(app.count_of(QStringLiteral("Mute Delay")) == 2);
    CHECK(undo.repeat_count() == 2);
    CHECK(events.count(EventKind::HAPTIC, "impact") == 2);

    // Undo from the toast takes the last tap back, and the toast closes.
    app.tap(QStringLiteral("toastUndo"));
    app.ui.settle();
    app.play();
    CHECK(app.count_of(QStringLiteral("Mute Delay")) == 1);

    // A note: type it, Add; the success haptic follows the commit, the field clears.
    events.clear();
    app.services.go_to(7, 10, 30, 5);
    app.ui.tap(app.need(QStringLiteral("noteInput")));
    app.ui.type(QStringLiteral("Caller asked for the next song"));
    app.ui.advance(16ms);
    CHECK(session.draft() == QStringLiteral("Caller asked for the next song"));
    app.tap(QStringLiteral("addButton"));
    app.ui.settle();
    CHECK(events.count(EventKind::HAPTIC, "success") == 1);
    CHECK(session.draft().isEmpty());
    CHECK(first_after(events, EventKind::HAPTIC, 0) > first_after(events, EventKind::COMMIT, 0));
    // The note's toast: Undo gives the text back to the composer.
    app.play();
    app.tap(QStringLiteral("toastUndo"));
    app.ui.settle();
    app.play();
    CHECK(session.draft() == QStringLiteral("Caller asked for the next song"));
    app.services.go_to(7, 10, 31, 0);
    app.tap(QStringLiteral("addButton"));
    app.ui.settle();
    app.services.advance(6s);
    app.ui.advance(300ms);

    // The "Last" bar opens Review on its tab.
    app.tap(QStringLiteral("logLastBar"));
    app.play();
    CHECK(app.current_tab() == REVIEW_TAB);
    CHECK(app.shown(QStringLiteral("reviewScreen")));
    CHECK(app.ui.find_items(QStringLiteral("reviewRow")).size() == 2);
    CHECK(app.need(QStringLiteral("reviewSwipe"))->isVisible());

    // A row opens Edit; the type page and the note are the sheet's own, so Cancel leaves the log as it was.
    app.ui.tap(app.need(QStringLiteral("reviewSwipe")));
    app.play();
    CHECK(app.need(QStringLiteral("editSheet"))->property("isOpen").toBool());
    // Delete from the sheet: the entry goes after the commit and the shell's toast offers Undo.
    app.tap(QStringLiteral("editDelete"));
    app.ui.settle();
    app.play();
    CHECK(session.live_entry_count() == 1);
    CHECK(undo.active());
    CHECK(app.shown(QStringLiteral("reviewToast")));
    CHECK_FALSE(app.shown(QStringLiteral("logToast")));
    app.ui.tap(app.ui.find_item(QStringLiteral("toastUndo"), app.need(QStringLiteral("reviewToast"))));
    app.ui.settle();
    app.play();
    CHECK(app.ui.find_items(QStringLiteral("reviewRow")).size() == 2);

    // Share: the sheet of the platform opens with the log as the export words it, and Review says so.
    app.tap(QStringLiteral("reviewShare"));
    app.ui.settle();
    app.play();
    CHECK(app.services.platform().sheets_presented() == 1);
    REQUIRE_FALSE(app.services.platform().shared_texts().empty());
    CHECK(app.services.platform().shared_texts().front().find("Mute Delay") != std::string::npos);

    // Settings, and back to Log.
    app.choose_tab(SETTINGS_TAB);
    CHECK(app.shown(QStringLiteral("settingsScreen")));
    app.choose_tab(LOG_TAB);
    CHECK(app.shown(QStringLiteral("logScreen")));
    CHECK(app.count_of(QStringLiteral("Mute Delay")) == 1);
}

TEST_CASE(
    "app.new_day: the new-day alert covers the app, shares first on request, starts the new log, and History lists the "
    "old one",
    "[ui][app]")
{
    App app;
    app.fill_header();
    REQUIRE(app.services.tap(role_of(*app.services.root().board(), 0, "typeId").toString()));
    app.ui.settle();
    app.choose_tab(HISTORY_TAB);
    CHECK(app.shown(QStringLiteral("historyScreen")));
    CHECK_FALSE(app.shown(QStringLiteral("historyRow")));
    app.choose_tab(LOG_TAB);
    CHECK_FALSE(app.need(QStringLiteral("newDay"))->property("isOpen").toBool());

    // Midnight passes with the log open: the alert is up, over the tab bar, and the app under it is unavailable.
    app.services.go_to(8, 8, 5);
    app.services.day().check();
    app.ui.settle();
    app.play();
    QQuickItem* alert = app.need(QStringLiteral("newDay"));
    REQUIRE(alert->property("isOpen").toBool());
    CHECK(parent_name(alert) == QStringLiteral("sheetHostLayer"));
    CHECK_FALSE(app.need(QStringLiteral("shellTabs"))->isEnabled());
    CHECK(app.services.day().prompt_visible());

    // "Share first" shares the log and leaves the alert up, without the warning and the button.
    QQuickItem* const alternate = app.ui.find_item(QStringLiteral("alertAlternate"), alert);
    REQUIRE(alternate != nullptr);
    CHECK(alternate->isVisible());
    app.ui.tap(alternate);
    app.ui.settle();
    app.play();
    CHECK(app.services.platform().sheets_presented() == 1);
    CHECK(alert->property("isOpen").toBool());
    CHECK_FALSE(app.services.day().unshared());

    // "Start a new log" archives the open one and the alert goes.
    app.ui.tap(app.ui.find_item(QStringLiteral("alertConfirm"), alert));
    app.ui.settle();
    app.play();
    CHECK_FALSE(app.services.day().prompt_visible());
    CHECK_FALSE(alert->property("isOpen").toBool());
    CHECK(app.need(QStringLiteral("shellTabs"))->isEnabled());

    // History reads its list when the tab is chosen: the old log is in it.
    app.choose_tab(HISTORY_TAB);
    app.ui.settle();
    app.play();
    CHECK(app.ui.find_items(QStringLiteral("historyRow")).size() == 1);
    CHECK_FALSE(app.shown(QStringLiteral("logScreen")));
}

TEST_CASE(
    "app.history: a row opens the past log on the tab's own stack, its toast is the shell's, and the tab going away "
    "closes it",
    "[ui][app]")
{
    App app;
    app.fill_header();
    REQUIRE(app.services.tap(role_of(*app.services.root().board(), 0, "typeId").toString()));
    app.ui.settle();
    app.services.go_to(8, 8, 5);
    app.services.day().check();
    app.ui.settle();
    REQUIRE(app.services.day().acceptNewLog());
    app.ui.settle();
    app.play();

    app.choose_tab(HISTORY_TAB);
    app.ui.settle();
    app.play();
    REQUIRE(app.ui.find_items(QStringLiteral("historyRow")).size() == 1);
    app.ui.tap(app.need(QStringLiteral("historySwipe")));
    app.ui.settle();
    app.play();
    QQuickItem* history = app.need(QStringLiteral("historyScreen"));
    CHECK(history->property("reviewShown").toBool());
    CHECK(app.shown(QStringLiteral("reviewList")));
    CHECK(app.shown(QStringLiteral("shellTabBar")));

    // Another tab, and the log is let go; coming back shows the list.
    app.choose_tab(SETTINGS_TAB);
    CHECK_FALSE(history->property("reviewShown").toBool());
    app.choose_tab(HISTORY_TAB);
    app.ui.settle();
    app.play();
    CHECK_FALSE(history->property("reviewShown").toBool());
    CHECK(app.shown(QStringLiteral("historyRow")));
}

TEST_CASE("app.settings: the import and export rows go through the chooser to the importer and the exporter",
          "[ui][app]")
{
    App app;
    app.choose_tab(SETTINGS_TAB);

    // Export: the row asks the chooser for a place, offering the name of a setup file, and the chosen path is written.
    app.tap(QStringLiteral("settingsImportExport"));
    app.play();
    app.tap(QStringLiteral("settingsExport"));
    CHECK(app.stub().property("saves").toInt() == 1);
    CHECK(app.stub().property("suggestedName").toString() == QStringLiteral("cs-bingo-board-setup.ejconfig"));
    const QString target = QStringLiteral("exports/cs-bingo-board-setup.ejconfig");
    app.choose_to_save(target);
    const auto& files = app.services.memory().config().files();
    REQUIRE(files.contains(target.toStdString()));
    CHECK(files.at(target.toStdString()).find("Wrong Response") != std::string::npos);

    // Import: the row asks for a file; the chosen file is read, the confirmation covers the tab bar, and Cancel changes
    // nothing.
    app.tap(QStringLiteral("settingsImport"));
    CHECK(app.stub().property("opens").toInt() == 1);
    app.choose_to_open(target, QStringLiteral("cs-bingo-board-setup.ejconfig"));
    CHECK(app.shown(QStringLiteral("importConfirm")));
    CHECK_FALSE(app.need(QStringLiteral("shellTabs"))->isEnabled());
    app.tap(QStringLiteral("cancelButton"));
    app.ui.settle();
    app.play();
    CHECK_FALSE(app.shown(QStringLiteral("importConfirm")));
    CHECK(app.need(QStringLiteral("shellTabs"))->isEnabled());
    CHECK(app.services.root().board()->rowCount() == 12);
}

TEST_CASE("app.messages: a failed write is one banner, except on the Log screen, which has its own", "[ui][app]")
{
    App app;
    auto fail = [&app](const QString& message)
    {
        REQUIRE(QMetaObject::invokeMethod(&app.services.root(), "writeFailed", Q_ARG(QString, QStringLiteral("test")),
                                          Q_ARG(QString, message)));
        app.ui.advance(50ms);
    };

    // On the Log screen the screen shows the failures of its own models, and the shell adds nothing.
    fail(QStringLiteral("The disk is full."));
    CHECK_FALSE(app.shown(QStringLiteral("writeFailedBanner")));

    app.choose_tab(REVIEW_TAB);
    fail(QStringLiteral("The disk is full."));
    CHECK(app.shown(QStringLiteral("writeFailedBanner")));
    // The banner is on top of the page, in the safe area.
    CHECK(app.ui.geometry(app.need(QStringLiteral("writeFailedBanner"))).top() >= 0);
    // Closed, it goes; a second failure brings it back with the new words.
    REQUIRE(QMetaObject::invokeMethod(app.need(QStringLiteral("writeFailedBanner")), "closeRequested"));
    app.ui.advance(50ms);
    CHECK_FALSE(app.shown(QStringLiteral("writeFailedBanner")));
    fail(QStringLiteral("The log could not be saved."));
    CHECK(app.shown(QStringLiteral("writeFailedBanner")));
    CHECK(app.need(QStringLiteral("writeFailedBanner"))->property("message").toString() ==
          QStringLiteral("The log could not be saved."));
}

TEST_CASE("app.theme: Dynamic Type and Reduce Motion reach the theme from the bridge", "[ui][app]")
{
    App app;
    QObject* theme = app.ui.theme();
    REQUIRE(theme != nullptr);
    CHECK(theme->property("textScale").toDouble() == 1.0);
    CHECK_FALSE(theme->property("reduceMotion").toBool());

    app.bridge.set_text_scale(1.3);
    app.bridge.set_reduce_motion(true);
    app.ui.advance(16ms);
    CHECK(theme->property("textScale").toDouble() == 1.3);
    CHECK(theme->property("reduceMotion").toBool());

    app.bridge.set_text_scale(1.0);
    app.bridge.set_reduce_motion(false);
    app.ui.advance(16ms);
    CHECK(theme->property("textScale").toDouble() == 1.0);
    CHECK_FALSE(theme->property("reduceMotion").toBool());
}

TEST_CASE("app.window_layout: the shell tells WindowLayout its size, so the class follows the window", "[ui][app]")
{
    App app;
    QObject* layout = app.ui.window_layout();
    REQUIRE(layout != nullptr);
    CHECK(layout->property("windowWidth").toDouble() == 402);
    CHECK(layout->property("compact").toBool());

    // The window and the shell change size as a rotation does. The rig is not told, so the class can only come from the
    // shell's binding.
    const auto resize = [&app](QSize size)
    {
        app.ui.window().resize(size);
        app.shell->setSize(QSizeF(size));
        app.ui.advance(16ms);
    };
    resize(QSize(1180, 820));
    CHECK(layout->property("windowWidth").toDouble() == 1180);
    CHECK(layout->property("windowHeight").toDouble() == 820);
    CHECK(layout->property("wide").toBool());
    resize(QSize(820, 1180));
    CHECK(layout->property("regular").toBool());
    resize(QSize(402, 874));
    CHECK(layout->property("compact").toBool());
}

TEST_CASE("app.hooks: the shell leaves Stage 5 the board-enabled state", "[ui][app]")
{
    App app;
    QQuickItem* shell = app.shell;
    CHECK(shell->property("boardEnabled").toBool());

    app.choose_tab(SETTINGS_TAB);
    CHECK_FALSE(shell->property("boardEnabled").toBool());
    app.choose_tab(LOG_TAB);
    CHECK(shell->property("boardEnabled").toBool());
}

TEST_CASE(
    "app.timing: five taps on the version text at the foot of Settings show the timing line, and five more hide it",
    "[ui][app]")
{
    App app;
    app.choose_tab(SETTINGS_TAB);

    // The foot of the list: the version and platform text, and no timing line.
    QQuickItem* about = app.need(QStringLiteral("settingsAbout"));
    CHECK(about->property("text").toString() == QStringLiteral("0.1.0 · test, storage: memory"));
    // It is at the end of a long list: scroll there, as the person does.
    QQuickItem* flickable = app.need(QStringLiteral("settingsFlickable"));
    flickable->setProperty("contentY", flickable->property("contentHeight").toDouble() - flickable->height());
    app.ui.layout();
    QQuickItem* footer = app.need(QStringLiteral("settingsAboutFooter"));
    CHECK_FALSE(app.shown(QStringLiteral("settingsTiming")));

    // Four taps do nothing; the fifth shows the line the bridge gives, which is the one the gate is read from.
    for (int i = 0; i < 4; ++i)
    {
        app.ui.tap(footer);
    }
    CHECK_FALSE(app.shown(QStringLiteral("settingsTiming")));
    app.ui.tap(footer);
    app.ui.advance(16ms);
    REQUIRE(app.shown(QStringLiteral("settingsTiming")));
    CHECK(app.need(QStringLiteral("settingsTiming"))->property("text").toString() ==
          QStringLiteral("launch 812 ms; tap 41 ms (last of 12), median 38 ms, worst 77 ms"));
    expect_clean(check_names(app.ui));

    // Five more hide it again.
    for (int i = 0; i < 5; ++i)
    {
        app.ui.tap(footer);
    }
    app.ui.advance(16ms);
    CHECK_FALSE(app.shown(QStringLiteral("settingsTiming")));
}

TEST_CASE("app.accessibility: the tree of each tab is named and in order in light and dark", "[ui][app]")
{
    App app;
    app.fill_header();
    REQUIRE(app.services.tap(role_of(*app.services.root().board(), 0, "typeId").toString()));
    app.ui.settle();
    app.services.advance(6s);
    app.ui.advance(300ms);

    const auto minimum = [](const Node& node)
    { return node.m_ObjectName.startsWith(QStringLiteral("tile")) ? 88.0 : 44.0; };
    for (const bool dark : {false, true})
    {
        CAPTURE(dark);
        app.ui.set_dark(dark);
        for (const int tab : {LOG_TAB, REVIEW_TAB, HISTORY_TAB, SETTINGS_TAB})
        {
            CAPTURE(tab);
            app.choose_tab(tab);
            app.ui.layout();
            expect_clean(check_names(app.ui));
            expect_clean(check_targets(app.ui, minimum));
            // The rig reads the rows of the Settings list that are scrolled out of the view (below the tab bar) as
            // items in view, and so finds them out of order after the tab bar; the order of the list itself is the
            // screen's test.
            if (tab != SETTINGS_TAB)
            {
                expect_clean(check_reading_order(app.ui));
            }
        }
        app.choose_tab(LOG_TAB);
    }
}

#include "app_flow_test.moc"

// ---- the keyboard on the phone's window (the bugs of TestFlight build 9) ----
//
// The window covers the whole screen under `Qt.ExpandedClientAreaHint`: the insets are the real ones (iPhone 17: 62 at
// the top, 34 at the bottom) and iOS reports the keyboard rectangle in the window's own coordinates, docked at its
// bottom. The tests above load the window of the offscreen platform, which has no insets and no keyboard, and a test
// that sets a height on a screen cannot see a screen that takes the keyboard from the wrong coordinates.

namespace
{

constexpr double PHONE_TOP = 62.0;
constexpr double PHONE_BOTTOM = 34.0;
constexpr double PHONE_KEYBOARD = 301.0;
constexpr double PHONE_HEIGHT = 874.0;
constexpr double KEYBOARD_TOP = PHONE_HEIGHT - PHONE_KEYBOARD;
/// A large sheet starts 5 pt above the safe area, as the design has it (`Theme.size.sheetTop` is 54 and the safe area
/// 59).
constexpr double SHEET_TOP = PHONE_TOP - 5.0;

/// The text input inside a field of the app: a FormField holds one named `formInput`, and the note composer's field is
/// a TextField itself.
QQuickItem* input_in(App& app, QQuickItem* field)
{
    REQUIRE(field != nullptr);
    QQuickItem* input = app.ui.find_item(QStringLiteral("formInput"), field);
    return input != nullptr ? input : field;
}

/// The person taps a field: it takes the focus, and the keyboard comes up from below the window (the "will show" report
/// of Qt, which has the final size) and arrives.
void focus_field(App& app, QQuickItem* field)
{
    QQuickItem* input = input_in(app, field);
    QMetaObject::invokeMethod(input, "forceActiveFocus");
    app.ui.show_keyboard_starting(PHONE_KEYBOARD);
    app.ui.advance(64ms);
    app.ui.show_keyboard(PHONE_KEYBOARD);
    app.ui.advance(300ms);
}

/// What every screen with a text field must do with the keyboard up: the focused field whole and above the keyboard and
/// not under the status bar; the area that the screen scrolls in down to the top of the keyboard, not a point short of
/// it, and not moved at its top (the bar above it is where it was).
void expect_clear_of_keyboard(App& app, QQuickItem* field, QQuickItem* area, double area_top_before)
{
    QQuickItem* input = input_in(app, field);
    INFO("field " << input->objectName().toStdString() << " of " << field->objectName().toStdString());
    REQUIRE(app.ui.focused_input() == input);
    const QRectF shown = app.ui.geometry(input);
    CHECK(shown.bottom() <= KEYBOARD_TOP);
    CHECK(shown.top() >= PHONE_TOP);
    if (area != nullptr)
    {
        const QRectF where = app.ui.geometry(area);
        CHECK(where.bottom() == KEYBOARD_TOP);
        CHECK(where.top() == area_top_before);
        CHECK(where.top() >= PHONE_TOP);
        CHECK(shown.top() >= where.top());
        CHECK(shown.bottom() <= where.bottom());
    }
}

/// Takes the keyboard down as the platform does when the person is done.
void put_keyboard_away(App& app)
{
    app.ui.hide_keyboard();
    app.ui.advance(100ms);
}

/// The phone's window for an app: the real insets.
void phone(App& app)
{
    app.ui.set_safe_area(PHONE_TOP, PHONE_BOTTOM);
    app.ui.advance(50ms);
}

/// The content area of the editor page that is in view: the pages under it on a stack are hidden.
QQuickItem* shown_page(App& app)
{
    for (QQuickItem* candidate : app.ui.find_items(QStringLiteral("editorPageContent")))
    {
        if (candidate->isVisible())
        {
            return candidate;
        }
    }
    FAIL("no editor page is shown");
    return nullptr;
}

/// A row of a list that may be under the tab bar or off the page: the row's own signal, which a tap sends.
void activate(App& app, const QString& name)
{
    QQuickItem* row = app.need(name);
    REQUIRE(QMetaObject::invokeMethod(row, "activated"));
    app.play();
}

QQuickItem* inside(App& app, const QString& name, QQuickItem* from)
{
    QQuickItem* item = app.ui.find_item(name, from);
    INFO(name.toStdString());
    REQUIRE(item != nullptr);
    return item;
}

} // namespace

TEST_CASE(
    "app.keyboard_details: Today's details ends at the keyboard for each field, from the top of the page and from "
    "the foot, with the bar in place and nothing under the status bar",
    "[ui][app]")
{
    App app;
    phone(app);
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    QQuickItem* screen = app.need(QStringLiteral("headerScreen"));
    QQuickItem* scroller = inside(app, QStringLiteral("headerScroller"), screen);
    QQuickItem* nav = inside(app, QStringLiteral("headerNav"), screen);
    const double top_before = app.ui.geometry(scroller).top();
    const double nav_top = app.ui.geometry(nav).top();
    CHECK(nav_top == PHONE_TOP);

    for (const char* name : {"headerUnit", "headerVersion", "headerCommit"})
    {
        for (const double scrolled : {0.0, 1.0})
        {
            CAPTURE(name, scrolled);
            QQuickItem* field = inside(app, QString::fromLatin1(name), screen);
            const double most = scroller->property("contentHeight").toDouble() - scroller->height();
            scroller->setProperty("contentY", -scroller->property("topMargin").toDouble() + scrolled * most);
            app.ui.advance(100ms);
            focus_field(app, field);
            expect_clear_of_keyboard(app, field, scroller, top_before);
            CHECK(app.ui.geometry(nav).top() == nav_top);
            put_keyboard_away(app);
        }
    }

    // The location typed by hand is a field of the page too.
    app.ui.tap(inside(app, QStringLiteral("headerOther"), screen));
    app.play();
    QQuickItem* other = inside(app, QStringLiteral("headerOtherText"), screen);
    focus_field(app, other);
    expect_clear_of_keyboard(app, other, scroller, top_before);
}

TEST_CASE("app.keyboard_version_sheet: the version sheet ends at the keyboard, with both fields whole above it",
          "[ui][app]")
{
    App app;
    phone(app);
    app.fill_header();
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    QQuickItem* screen = app.need(QStringLiteral("headerScreen"));
    QQuickItem* sheet = inside(app, QStringLiteral("versionSheet"), screen);
    REQUIRE(QMetaObject::invokeMethod(sheet, "show"));
    app.play();
    QQuickItem* area = sheet->property("contentArea").value<QQuickItem*>();
    REQUIRE(area != nullptr);
    for (const char* name : {"versionTo", "versionCommit"})
    {
        CAPTURE(name);
        QQuickItem* field = inside(app, QString::fromLatin1(name), sheet);
        focus_field(app, field);
        QQuickItem* input = input_in(app, field);
        REQUIRE(app.ui.focused_input() == input);
        CHECK(app.ui.geometry(input).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(input).top() >= PHONE_TOP);
        CHECK(app.ui.geometry(area).bottom() == KEYBOARD_TOP);
        // The sheet is shorter, never lifted into the status bar.
        CHECK(app.ui.geometry(inside(app, QStringLiteral("sheetPanel"), sheet)).top() >= SHEET_TOP);
        put_keyboard_away(app);
    }
}

TEST_CASE(
    "app.keyboard_log: the note composer is above the keyboard and the Log screen does not move, and the template "
    "value sheet and the Other picker end at the keyboard",
    "[ui][app]")
{
    App app;
    phone(app);
    app.fill_header();
    QQuickItem* log = app.need(QStringLiteral("logScreen"));
    QQuickItem* card = inside(app, QStringLiteral("logHeaderCard"), log);
    const double card_top = app.ui.geometry(card).top();
    CHECK(card_top >= PHONE_TOP);

    SECTION("the note composer")
    {
        QQuickItem* note = inside(app, QStringLiteral("noteInput"), log);
        focus_field(app, note);
        REQUIRE(app.ui.focused_input() == note);
        CHECK(app.ui.geometry(note).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(note).top() >= PHONE_TOP);
        // Nothing is scrolled: the screen is as it was, the card under the Dynamic Island and not behind it.
        CHECK(app.ui.geometry(card).top() == card_top);
    }

    SECTION("the template value sheet")
    {
        std::vector<QQuickItem*> chips = app.ui.find_items(QStringLiteral("chip"), log);
        REQUIRE_FALSE(chips.empty());
        app.ui.tap(chips.front());
        app.play();
        QQuickItem* sheet = app.need(QStringLiteral("logTemplateSheet"));
        REQUIRE(sheet->property("isOpen").toBool());
        QQuickItem* field = inside(app, QStringLiteral("templateValueField"), sheet);
        focus_field(app, field);
        QQuickItem* area = sheet->property("contentArea").value<QQuickItem*>();
        CHECK(app.ui.geometry(input_in(app, field)).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(area).bottom() == KEYBOARD_TOP);
        CHECK(app.ui.geometry(inside(app, QStringLiteral("sheetPanel"), sheet)).top() >= SHEET_TOP);
    }

    SECTION("the Other picker's form for a new error type")
    {
        app.tap(QStringLiteral("tile11"));
        app.play();
        QQuickItem* picker = app.need(QStringLiteral("otherPicker"));
        REQUIRE(picker->property("isOpen").toBool());
        app.ui.tap(inside(app, QStringLiteral("pickerAdd"), picker));
        app.play();
        QQuickItem* field = inside(app, QStringLiteral("pickerNewName"), picker);
        focus_field(app, field);
        QQuickItem* area = picker->property("contentArea").value<QQuickItem*>();
        CHECK(app.ui.geometry(input_in(app, field)).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(area).bottom() == KEYBOARD_TOP);
        CHECK(app.ui.geometry(inside(app, QStringLiteral("sheetPanel"), picker)).top() >= SHEET_TOP);
    }
}

TEST_CASE("app.keyboard_review: the note of Edit entry is above the keyboard and the sheet ends at it", "[ui][app]")
{
    App app;
    app.fill_header();
    REQUIRE(app.services.tap(role_of(*app.services.root().board(), 0, "typeId").toString()));
    app.ui.settle();
    app.choose_tab(REVIEW_TAB);
    phone(app);
    app.ui.tap(app.need(QStringLiteral("reviewSwipe")));
    app.play();
    QQuickItem* sheet = app.need(QStringLiteral("editSheet"));
    REQUIRE(sheet->property("isOpen").toBool());
    QQuickItem* field = inside(app, QStringLiteral("editNote"), sheet);
    focus_field(app, field);
    QQuickItem* area = sheet->property("contentArea").value<QQuickItem*>();
    CHECK(app.ui.geometry(input_in(app, field)).bottom() <= KEYBOARD_TOP);
    CHECK(app.ui.geometry(area).bottom() == KEYBOARD_TOP);
    CHECK(app.ui.geometry(inside(app, QStringLiteral("sheetPanel"), sheet)).top() >= SHEET_TOP);
}

TEST_CASE(
    "app.keyboard_settings: the facilitator page, the editors of locations and note templates, and the sheets of the "
    "error types and of a location end at the keyboard",
    "[ui][app]")
{
    App app;
    // The tab is chosen first: the Log screen of the test machine's fonts is taller than the phone's, and its board
    // reaches under the tab bar of the window with its real insets.
    app.choose_tab(SETTINGS_TAB);
    phone(app);
    QQuickItem* settings = app.need(QStringLiteral("settingsScreen"));

    SECTION("the facilitator page: the page ends at the keyboard, and a field that is clear does not scroll it")
    {
        activate(app, QStringLiteral("settingsFacilitator"));
        QQuickItem* field = inside(app, QStringLiteral("settingsTextField"), settings);
        // The page in view is the last one of the stack: the list under it has a Flickable of the same name.
        const auto flicks = app.ui.find_items(QStringLiteral("settingsFlickable"), settings);
        REQUIRE_FALSE(flicks.empty());
        QQuickItem* flick = flicks.back();
        const double top_before = app.ui.geometry(flick).top();
        focus_field(app, field);
        CHECK(app.ui.geometry(flick).bottom() == KEYBOARD_TOP);
        CHECK(app.ui.geometry(flick).top() == top_before);
        CHECK(app.ui.geometry(input_in(app, field)).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(input_in(app, field)).top() >= PHONE_TOP);
        // The title bar of the page is where it was: nothing scrolled.
        CHECK(flick->property("contentY").toDouble() == 0.0);
    }

    SECTION("the locations editor, and the sheet that renames a location")
    {
        activate(app, QStringLiteral("settingsLocations"));
        QQuickItem* page = shown_page(app);
        const double top_before = app.ui.geometry(page).top();
        QQuickItem* field = inside(app, QStringLiteral("newLocationField"), page);
        focus_field(app, field);
        expect_clear_of_keyboard(app, field, page, top_before);
        put_keyboard_away(app);

        app.ui.tap(app.ui.find_items(QStringLiteral("locationRow"), page).front());
        app.play();
        QQuickItem* rename = app.need(QStringLiteral("renameField"));
        focus_field(app, rename);
        QQuickItem* sheet = app.ui.find_item(QStringLiteral("sheetPanel"));
        REQUIRE(sheet != nullptr);
        CHECK(app.ui.geometry(input_in(app, rename)).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(sheet).top() >= SHEET_TOP);
    }

    SECTION("the note template editor")
    {
        activate(app, QStringLiteral("settingsTemplates"));
        REQUIRE(QMetaObject::invokeMethod(app.ui.find_items(QStringLiteral("templateRow")).front(), "activated"));
        app.play();
        QQuickItem* page = shown_page(app);
        const double top_before = app.ui.geometry(page).top();
        for (const char* name : {"nameField", "promptField", "patternField"})
        {
            CAPTURE(name);
            QQuickItem* field = inside(app, QString::fromLatin1(name), page);
            focus_field(app, field);
            expect_clear_of_keyboard(app, field, page, top_before);
            put_keyboard_away(app);
        }
    }

    SECTION("the sheet of a new error type")
    {
        activate(app, QStringLiteral("settingsErrorTypes"));
        app.ui.tap(app.ui.find_items(QStringLiteral("addRow")).front());
        app.play();
        QQuickItem* field = app.need(QStringLiteral("nameField"));
        focus_field(app, field);
        QQuickItem* sheet = app.ui.find_item(QStringLiteral("sheetPanel"));
        REQUIRE(sheet != nullptr);
        CHECK(app.ui.geometry(input_in(app, field)).bottom() <= KEYBOARD_TOP);
        CHECK(app.ui.geometry(sheet).top() >= SHEET_TOP);
    }
}

TEST_CASE("app.keyboard_first_launch: setting up by hand ends the page at the keyboard, with Continue just above it",
          "[ui][app]")
{
    App app(false);
    phone(app);
    app.tap(QStringLiteral("manualCard"));
    app.play();
    QQuickItem* page = shown_page(app);
    const double top_before = app.ui.geometry(page).top();
    for (const char* name : {"facilitatorField", "prefixField"})
    {
        CAPTURE(name);
        QQuickItem* field = inside(app, QString::fromLatin1(name), page);
        focus_field(app, field);
        expect_clear_of_keyboard(app, field, page, top_before);
        // Continue is above the keyboard and is not pushed past it.
        QQuickItem* proceed = inside(app, QStringLiteral("continueButton"), page);
        CHECK(app.ui.geometry(proceed).bottom() <= KEYBOARD_TOP);
        put_keyboard_away(app);
    }
}

// ---- the keyboard does not stay (build 9: it stayed over the Log screen after Today's details was left) ----
//
// A field has the focus and the keyboard is up. Leaving the page, changing the tab or dismissing the sheet that holds
// the field must end the typing: no text field keeps the focus, the keyboard goes, and what was typed is saved by the
// field's own focus change. The rig's keyboard goes only when the app puts it away (`Keyboard.hide()`), as the phone's
// does not go by itself when the focus moves to something that takes no text, so a keyboard that is still up is a
// failure of the app.

namespace
{

/// The first item with this name that is shown (a page on a stack that is under another is hidden).
QQuickItem* shown_item(App& app, const QString& name)
{
    for (QQuickItem* candidate : app.ui.find_items(name))
    {
        if (candidate->isVisible() && candidate->width() > 0)
        {
            return candidate;
        }
    }
    FAIL("nothing named " << name.toStdString() << " is shown");
    return nullptr;
}

void expect_no_typing(App& app)
{
    CHECK(app.ui.focused_input() == nullptr);
    CHECK_FALSE(app.ui.keyboard_up());
}

QString unit_text(App& app)
{
    return app.services.root().session()->property("unitText").toString();
}

} // namespace

TEST_CASE("app.keyboard_back: going back from Today's details puts the keyboard away, for a field of digits and for a "
          "text field, and saves what was typed",
          "[ui][app]")
{
    App app;
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    QQuickItem* screen = app.need(QStringLiteral("headerScreen"));

    SECTION("the unit number, which has the number pad")
    {
        QQuickItem* field = inside(app, QStringLiteral("headerUnit"), screen);
        focus_field(app, field);
        app.ui.type(QStringLiteral("27"));
        app.ui.advance(16ms);
        REQUIRE(app.ui.keyboard_up());
        app.ui.tap(inside(app, QStringLiteral("navLeading"), screen));
        app.play();
        CHECK(app.shown(QStringLiteral("logScreen")));
        expect_no_typing(app);
        CHECK(app.ui.keyboard_hides() >= 1);
        app.ui.settle();
        CHECK(unit_text(app).contains(QStringLiteral("27")));
    }

    SECTION("the commit, which has the letters")
    {
        QQuickItem* field = inside(app, QStringLiteral("headerCommit"), screen);
        focus_field(app, field);
        app.ui.type(QStringLiteral("a1b2c3d"));
        app.ui.advance(16ms);
        REQUIRE(app.ui.keyboard_up());
        app.ui.tap(inside(app, QStringLiteral("navLeading"), screen));
        app.play();
        CHECK(app.shown(QStringLiteral("logScreen")));
        expect_no_typing(app);
    }
}

TEST_CASE(
    "app.keyboard_picker: a row that opens a sheet ends the typing in the field beside it, and the sheet gives the "
    "focus back to the row and never to the field",
    "[ui][app]")
{
    App app;
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    QQuickItem* screen = app.need(QStringLiteral("headerScreen"));
    QQuickItem* unit = inside(app, QStringLiteral("headerUnit"), screen);
    QQuickItem* row = inside(app, QStringLiteral("headerEnvironment"), screen);

    // The sequence of the owner: type the unit, pick an environment, go back.
    focus_field(app, unit);
    app.ui.type(QStringLiteral("27"));
    app.ui.advance(16ms);
    REQUIRE(app.ui.keyboard_up());

    // Tapping the row ends the typing before the sheet is up: the field is saved and the keyboard goes.
    app.ui.tap(row);
    app.ui.advance(16ms);
    CHECK(app.ui.focused_input() == nullptr);
    CHECK_FALSE(app.ui.keyboard_up());
    app.ui.settle();
    CHECK(unit_text(app).contains(QStringLiteral("27")));
    app.play();
    QQuickItem* sheet = inside(app, QStringLiteral("environmentSheet"), screen);
    REQUIRE(sheet->property("isOpen").toBool());
    expect_no_typing(app);

    // Prod is picked and the sheet closes: the number pad does not come back, and the focus is on the row that opened
    // it.
    app.ui.tap(inside(app, QStringLiteral("environment_prod"), sheet));
    app.play();
    CHECK_FALSE(sheet->property("isOpen").toBool());
    expect_no_typing(app);
    CHECK(row->hasActiveFocus());
    CHECK(inside(app, QStringLiteral("headerEnvironment"), screen)->property("value").toString() ==
          QStringLiteral("prod"));

    // Back to Log: still nothing up.
    app.ui.tap(inside(app, QStringLiteral("navLeading"), screen));
    app.play();
    CHECK(app.shown(QStringLiteral("logScreen")));
    expect_no_typing(app);
}

TEST_CASE("app.keyboard_tabs: changing the tab, and pushing or popping a page, puts the keyboard away", "[ui][app]")
{
    App app;

    SECTION("a tab change while a field of Settings has the focus")
    {
        app.choose_tab(SETTINGS_TAB);
        activate(app, QStringLiteral("settingsFacilitator"));
        QQuickItem* field =
            inside(app, QStringLiteral("settingsTextField"), app.need(QStringLiteral("settingsScreen")));
        focus_field(app, field);
        REQUIRE(app.ui.keyboard_up());
        app.choose_tab(LOG_TAB);
        expect_no_typing(app);
    }

    SECTION("Back of a Settings editor, with the new location being typed")
    {
        app.choose_tab(SETTINGS_TAB);
        activate(app, QStringLiteral("settingsLocations"));
        QQuickItem* page = shown_page(app);
        focus_field(app, inside(app, QStringLiteral("newLocationField"), page));
        REQUIRE(app.ui.keyboard_up());
        app.ui.tap(shown_item(app, QStringLiteral("navLeading")));
        app.play();
        expect_no_typing(app);
    }

    SECTION("the note being typed on Log, and Today's details opened from the card")
    {
        focus_field(app, app.need(QStringLiteral("noteInput")));
        REQUIRE(app.ui.keyboard_up());
        app.tap(QStringLiteral("logHeaderCard"));
        app.play();
        CHECK(app.shown(QStringLiteral("headerScreen")));
        expect_no_typing(app);
    }

    SECTION("the editor of a note template, popped back to its list")
    {
        app.choose_tab(SETTINGS_TAB);
        activate(app, QStringLiteral("settingsTemplates"));
        REQUIRE(QMetaObject::invokeMethod(app.ui.find_items(QStringLiteral("templateRow")).front(), "activated"));
        app.play();
        focus_field(app, inside(app, QStringLiteral("nameField"), shown_page(app)));
        REQUIRE(app.ui.keyboard_up());
        app.ui.tap(shown_item(app, QStringLiteral("navLeading")));
        app.play();
        expect_no_typing(app);
    }
}

TEST_CASE(
    "app.keyboard_sheets: dismissing a sheet with a field in it puts the keyboard away and does not give the focus to "
    "a field under it",
    "[ui][app]")
{
    App app;
    // The real insets: the sheet starts at the status bar, so the scrim is there to be tapped over it.
    phone(app);
    app.fill_header();
    app.tap(QStringLiteral("logHeaderCard"));
    app.play();
    QQuickItem* screen = app.need(QStringLiteral("headerScreen"));
    QQuickItem* version = inside(app, QStringLiteral("headerVersion"), screen);

    // The version field has the focus, and the check mark beside it opens the sheet, which has fields of its own.
    focus_field(app, version);
    QQuickItem* sheet = inside(app, QStringLiteral("versionSheet"), screen);
    REQUIRE(QMetaObject::invokeMethod(sheet, "show"));
    app.play();
    focus_field(app, inside(app, QStringLiteral("versionTo"), sheet));
    REQUIRE(app.ui.keyboard_up());

    SECTION("Cancel")
    {
        app.ui.tap(inside(app, QStringLiteral("navLeading"), sheet));
        app.ui.advance(1000ms);
    }

    SECTION("a tap on the scrim")
    {
        QQuickItem* scrim = inside(app, QStringLiteral("sheetScrim"), sheet);
        app.ui.tap(scrim, QPointF(scrim->width() / 2, 20));
        app.ui.advance(1000ms);
    }

    SECTION("close()")
    {
        REQUIRE(QMetaObject::invokeMethod(sheet, "close"));
        app.ui.advance(1000ms);
    }

    CHECK_FALSE(sheet->property("isOpen").toBool());
    expect_no_typing(app);
    CHECK_FALSE(version->property("hasFocus").toBool());
}
