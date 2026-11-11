// SPDX-License-Identifier: GPL-3.0-or-later
// The visual capture (not a test of behaviour): renders each state of the app that has a mock-up in
// docs/design/screens, at 402 x 874 points (the iPad mock-ups of Stage 8 at 820 x 1180 and 1180 x 820) in light and
// dark, and writes PNGs for the comparison of the mock-ups with the app (tools/visual/).
//
// It is hidden: the tag [.capture] keeps it out of every ctest run (`catch_discover_tests` lists the cases that
// `TEST_SPEC "~[perf]"` selects, and Catch2 does not select a hidden case with an exclusion-only spec; `ctest -N` shows
// no `capture.` case) and the case skips itself unless EJ_CAPTURE_DIR is set. Run it with tools/visual/run_capture.py,
// or
//
//     EJ_CAPTURE_DIR=out EJ_CAPTURE_FONTS=build/visual/fonts QT_SCALE_FACTOR=2 ej_ui_tests "capture.*"
//
// Environment: EJ_CAPTURE_DIR (the folder for the PNGs, required), EJ_CAPTURE_FONTS (a folder of .ttf files to
// register; the first family found becomes the application font; without it the font of the platform), EJ_CAPTURE_ONLY
// (a comma separated list of mock-up names, for a quick run). QT_SCALE_FACTOR=2 makes the PNGs 804 x 1748 (the scale of
// the mock-up renders); the layout stays 402 x 874 points.
//
// The app is the real shell over the real Stage 3 services (the in-memory store, a fake clock), driven as the person
// drives it: taps, a swipe, typing. The shell sits under an item that adds the safe-area insets of the device (an
// iPhone 17: 59 pt at the top, 34 pt at the bottom; an iPad: 24 and 20), which the offscreen window does not have.
//
// The project and Stage 3 headers come before the Qt Quick ones, because the core headers define a name that the
// Windows headers behind Qt Quick define as a macro. clang-format would sort them the other way, so it is switched off.
// clang-format off
#include "about_support.hpp"
#include "support/rig.hpp"
#include "viewmodel/board_model.hpp"
#include "viewmodel/day_watcher.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/share_service.hpp"
#include "viewmodel/undo_controller.hpp"

#include "rig/ui_rig.hpp"
// clang-format on

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <QAbstractItemModel>
#include <QByteArray>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QHash>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaObject>
#include <QObject>
#include <QPointF>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <vector>

using namespace ej;
using namespace ej::test;
using namespace ej::ui;
using namespace std::chrono_literals;

namespace
{

constexpr auto MODULE = "ErrorJournalingApp";
constexpr int REVIEW_TAB = 1;
constexpr int HISTORY_TAB = 2;
constexpr int SETTINGS_TAB = 3;

/// The window a mock-up is drawn for: its size in points, its safe-area insets, and the suffix of its mock-up's name.
struct Device
{
    QString m_Suffix;
    QSize m_Size;
    int m_InsetTop = 0;
    int m_InsetBottom = 0;
};

/// An iPhone 17 in portrait (the mock-ups leave 59 pt at the top; the tab bar is 83 pt with the 34 pt home-indicator
/// inset), and the iPad Air 11-inch of the Stage 8 mock-ups (a 24 pt status bar and a 20 pt home indicator).
const Device PHONE{QString(), QSize(402, 874), 59, 34};
const Device IPAD_PORTRAIT{QStringLiteral(".ipad-portrait"), QSize(820, 1180), 24, 20};
const Device IPAD_LANDSCAPE{QStringLiteral(".ipad-landscape"), QSize(1180, 820), 24, 20};

/// The shell, under an item that adds the safe-area insets of the phone, which every SafeFrame below it reads.
const char* const WRAPPER = R"(
import QtQuick
import ErrorJournalingApp

Item {
    id: wrap

    required property var session
    required property var undo
    required property var board
    required property var picker
    required property var timeline
    required property var history
    required property var day
    required property var settings
    required property var share
    required property var appRoot
    required property var bridge
    required property var fileChooser
    required property var about
    required property string aboutText

    SafeArea.additionalMargins.top: %1
    SafeArea.additionalMargins.bottom: %2

    AppShell {
        anchors.fill: parent
        session: wrap.session
        undo: wrap.undo
        board: wrap.board
        picker: wrap.picker
        timeline: wrap.timeline
        history: wrap.history
        day: wrap.day
        settings: wrap.settings
        share: wrap.share
        appRoot: wrap.appRoot
        bridge: wrap.bridge
        fileChooser: wrap.fileChooser
        about: wrap.about
        aboutText: wrap.aboutText
    }
}
)";

/// What the window shows while the services are being rebuilt.
const QByteArray EMPTY_ITEM = "import QtQuick\nItem {}\n";

/// The part of the bridge the shell and the screens call, with the names of `QmlBridge`.
class CaptureBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double textScale READ text_scale WRITE set_text_scale NOTIFY textScaleChanged)
    Q_PROPERTY(bool reduceMotion READ reduce_motion WRITE set_reduce_motion NOTIFY reduceMotionChanged)
    Q_PROPERTY(QString timingSummary READ timing_summary NOTIFY timingChanged)

public:
    explicit CaptureBridge(FakePlatform& platform) : m_Platform(platform) {}

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
        return {};
    }

    Q_INVOKABLE void prepareHaptics()
    {
        m_Platform.prepare();
    }
    Q_INVOKABLE void setBoardVisible(bool /*visible*/) {}
    Q_INVOKABLE void setApplicationActive(bool /*active*/) {}
    Q_INVOKABLE void hapticImpact(double /*pressTimestamp*/ = 0)
    {
        m_Platform.fire(FakePlatform::haptic_kind::IMPACT);
    }
    Q_INVOKABLE void hapticSuccess()
    {
        m_Platform.fire(FakePlatform::haptic_kind::SUCCESS);
    }

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

/// Registers the fonts of EJ_CAPTURE_FONTS and makes the first family found the application font. Returns the family,
/// empty when there was nothing to register. The offscreen platform has no system fonts to speak of on this machine, so
/// without a font the text is boxes.
QString register_fonts()
{
    const QString folder = qEnvironmentVariable("EJ_CAPTURE_FONTS");
    if (folder.isEmpty())
    {
        return {};
    }
    QString family;
    const QFileInfoList files =
        QDir(folder).entryInfoList({QStringLiteral("*.ttf"), QStringLiteral("*.otf")}, QDir::Files);
    for (const QFileInfo& file : files)
    {
        const int id = QFontDatabase::addApplicationFont(file.absoluteFilePath());
        if (id >= 0 && family.isEmpty())
        {
            const QStringList families = QFontDatabase::applicationFontFamilies(id);
            if (!families.isEmpty())
            {
                family = families.front();
            }
        }
    }
    if (!family.isEmpty())
    {
        QGuiApplication::setFont(QFont(family));
    }
    // The log line of the version sheet is monospaced (Theme.font.monospace takes the first of Menlo, Cascadia Mono and
    // Consolas that exists), and the offscreen platform has no system fonts, so the platform's own face is added too.
    for (const char* path : {"C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/CascadiaMono.ttf",
                             "/System/Library/Fonts/Menlo.ttc", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"})
    {
        if (QFile::exists(QString::fromLatin1(path)) &&
            QFontDatabase::addApplicationFont(QString::fromLatin1(path)) >= 0)
        {
            break;
        }
    }
    return family;
}

/// How a scene starts: on which November day the fake clock begins (the open log is made on that day), and whether the
/// setup has been imported (a first launch has not).
struct Start
{
    int m_Day = 7;
    bool m_Configured = true;
};

/// The application as the device has it, under its insets: the services over the fakes, the shell on top, and a file
/// chooser that shows no dialog. Named for the phone, which most mock-ups are drawn for.
struct Phone
{
    explicit Phone(const Start& start, const Device& device = PHONE)
        : services(RigStore::MEMORY), bridge(services.platform()), ui(device.m_Size), m_Device(device)
    {
        services.go_to(start.m_Day, 8, 55);
        if (start.m_Configured)
        {
            services.import_sample_configuration();
        }
        ui.on_advance([this](std::chrono::milliseconds by) { services.advance(by); });
        ui.on_settle([this] { services.settle(); });
        QQmlComponent component(&ui.engine(), MODULE, "UiStubFileChooser");
        chooser.reset(component.create());
        REQUIRE(chooser != nullptr);
        load();
    }

    Phone(const Phone&) = delete;
    Phone& operator=(const Phone&) = delete;

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
                           {QStringLiteral("about"), object(about)},
                           {QStringLiteral("aboutText"), QStringLiteral("0.1.0")}};
    }

    void load()
    {
        const QByteArray source =
            QString::fromUtf8(WRAPPER).arg(m_Device.m_InsetTop).arg(m_Device.m_InsetBottom).toUtf8();
        QQuickItem* wrapper = ui.load_source(source, properties());
        INFO(ui.errors().toStdString());
        REQUIRE(wrapper != nullptr);
        ui.settle();
        ui.advance(50ms);
    }

    /// The services are rebuilt on the same store, as a relaunch does; the shell is made again over them.
    void relaunch(const std::function<void()>& before_restart = {})
    {
        REQUIRE(ui.load_source(EMPTY_ITEM) != nullptr);
        if (before_restart)
        {
            before_restart();
        }
        services.restart();
        load();
    }

    /// The first item with this objectName that is shown (the tabs that are not in view keep their screens, which
    /// share names with the screen in view), else the first.
    [[nodiscard]] QQuickItem* need(const QString& name, QQuickItem* from = nullptr)
    {
        const auto items = ui.find_items(name, from);
        INFO(name.toStdString());
        REQUIRE_FALSE(items.empty());
        for (QQuickItem* item : items)
        {
            if (item->isVisible() && item->width() > 0)
            {
                return item;
            }
        }
        return items.front();
    }

    /// The sheet that holds `inner` (the item named `sheetPanel` above it); the sheets of the shell are in one layer,
    /// so a name inside a sheet is looked for under it.
    [[nodiscard]] QQuickItem* sheet_of(QQuickItem* inner)
    {
        for (QQuickItem* item = inner; item != nullptr; item = item->parentItem())
        {
            if (item->objectName() == QStringLiteral("sheetPanel"))
            {
                return item;
            }
        }
        FAIL("not inside a sheet");
        return nullptr;
    }

    /// The `index`th item with this objectName (the delegates of a list share one).
    [[nodiscard]] QQuickItem* nth(const QString& name, std::size_t index, QQuickItem* from = nullptr)
    {
        const auto items = ui.find_items(name, from);
        INFO(name.toStdString());
        REQUIRE(items.size() > index);
        return items[index];
    }

    void tap(const QString& name, QQuickItem* from = nullptr)
    {
        ui.tap(need(name, from));
        ui.advance(16ms);
    }

    void tap_item(QQuickItem* item)
    {
        ui.tap(item);
        ui.advance(16ms);
    }

    /// Lets the transition of a page or a sheet finish.
    void play()
    {
        ui.settle();
        ui.advance(500ms);
        ui.settle();
    }

    void choose_tab(int tab)
    {
        static const char* const NAMES[] = {"tabLog", "tabReview", "tabHistory", "tabSettings"};
        tap(QString::fromLatin1(NAMES[tab]));
        play();
    }

    /// Types into a form field (a `FormField`, whose input is named `formInput`), as the person does: tap, then type.
    void type_into(const QString& field, const QString& text, QQuickItem* from = nullptr)
    {
        QQuickItem* container = need(field, from);
        QQuickItem* input = ui.find_item(QStringLiteral("formInput"), container);
        ui.tap(input != nullptr ? input : container);
        ui.advance(16ms);
        ui.type(text);
        ui.advance(50ms);
    }

    /// A drag to the left over a swipe row, which reveals its actions.
    void swipe_open(QQuickItem* row)
    {
        const double y = row->height() / 2;
        ui.swipe(row, QPointF(row->width() - 12, y), QPointF(row->width() * 0.15, y));
        ui.advance(500ms);
    }

    /// The `typeId` of the board tile with this label.
    [[nodiscard]] QString type_id(const QString& label)
    {
        const int row = row_with_label(*services.root().board(), label);
        REQUIRE(row >= 0);
        return role_of(*services.root().board(), row, "typeId").toString();
    }

    Rig services;
    CaptureBridge bridge;
    /// The About pages: the build of the mock-ups (57, a1b2c3d, develop) and notices that name Qt Base as the mock-up
    /// does.
    ej::viewmodel::AboutModel about{ej::test::about_facts(),
                                    [] { return ej::test::about_notices(6, QStringLiteral("Qt Base")); }};
    UiRig ui;
    std::unique_ptr<QObject> chooser;
    Device m_Device;
};

// ---- the data of the scenes: a day as the mock-ups show it, through the services the app has ----

/// Today's details, filled in as the header screen's fields would be (the screen has its own tests).
void fill_header(Phone& phone, const QString& unit = QStringLiteral("27"),
                 const QString& location = QStringLiteral("North Site"),
                 const QString& version = QStringLiteral("1.4.2"), const QString& commit = QStringLiteral("a1b2c3d"))
{
    auto& session = phone.services.session();
    REQUIRE(session.correctHeader(QStringLiteral("unit_number"), unit));
    auto& locations = *phone.services.root().settings()->locations();
    const int row = row_with_label(locations, location, "name");
    REQUIRE(row >= 0);
    REQUIRE(session.correctHeader(QStringLiteral("location"), role_of(locations, row, "locationId").toString()));
    // A commit too, as the mock-ups show one, so that the Log card has no "still to fill" strip (HDR-09).
    REQUIRE(session.correctVersion(version, commit));
    phone.ui.settle();
}

/// An error tile, tapped at `day` hh:mm:ss of November 2026 (the haptic first, then `recordError`, as the screen does).
void record(Phone& phone, const QString& label, int day, int hour, int minute, int second = 0)
{
    phone.services.go_to(day, hour, minute, second);
    REQUIRE(phone.services.tap(phone.type_id(label)));
    phone.services.settle();
}

void note(Phone& phone, const QString& text, int day, int hour, int minute)
{
    phone.services.go_to(day, hour, minute, 30);
    REQUIRE(phone.services.add_note(text));
    phone.services.settle();
}

/// A note through the template chip "Played song", as the chip's value prompt makes it.
void played_song(Phone& phone, const QString& value, int day, int hour, int minute)
{
    phone.services.go_to(day, hour, minute, 30);
    auto& templates = *phone.services.root().settings()->templates();
    const int row = row_with_label(templates, QStringLiteral("Played song"), "name");
    REQUIRE(row >= 0);
    REQUIRE(phone.services.session().addTemplatedNote(role_of(templates, row, "templateId").toString(), value));
    phone.services.settle();
}

/// The note that an error entry carries, set the way Edit entry sets it (`saveEntry`).
void attach_note(Phone& phone, int timeline_row, const QString& text)
{
    auto& timeline = phone.services.timeline();
    const QString id = role_of(timeline, timeline_row, "entryId").toString();
    QVariantMap edit = timeline.entryForEdit(id);
    REQUIRE_FALSE(edit.isEmpty());
    edit.insert(QStringLiteral("attachedNote"), text);
    REQUIRE(timeline.saveEntry(id, edit));
    phone.services.settle();
}

/// The day of the Log, LogBanner, Recovery, WriteFailed and TemplateValue mock-ups: seven entries, two of them notes,
/// and the last one is Mute Delay at 10:20 (the "Last" bar and the toast say so). The counts are Wrong Response 2,
/// Wrong Track Played 1, Speech Delay 1 and Mute Delay 1.
void log_day(Phone& phone)
{
    record(phone, QStringLiteral("Wrong Response"), 7, 9, 0);
    record(phone, QStringLiteral("Wrong Response"), 7, 9, 15);
    record(phone, QStringLiteral("Wrong Track Played"), 7, 9, 40);
    played_song(phone, QStringLiteral("Morning Medley"), 7, 9, 50);
    record(phone, QStringLiteral("Speech Delay"), 7, 9, 58);
    note(phone, QStringLiteral("Played Evening Song"), 7, 10, 5);
    record(phone, QStringLiteral("Mute Delay"), 7, 10, 20);
}

/// The day of the Review and Edit entry mock-ups: seven entries, in the order and at the times that Review shows.
void review_day(Phone& phone)
{
    record(phone, QStringLiteral("Wrong Response"), 7, 9, 42);
    record(phone, QStringLiteral("Wrong Response"), 7, 10, 5);
    record(phone, QStringLiteral("Mute Delay"), 7, 10, 20);
    played_song(phone, QStringLiteral("Morning Medley"), 7, 10, 30);
    record(phone, QStringLiteral("Wrong Track Played"), 7, 11, 2);
    attach_note(phone, 4, QStringLiteral("asked for Evening Song"));
    record(phone, QStringLiteral("Speech Delay"), 7, 11, 40);
    note(phone, QStringLiteral("Played Evening Song"), 7, 12, 10);
}

/// The day of the board mock-ups with the longest label: "Sequence Out of Order" three times, Mute Delay once.
void label_day(Phone& phone)
{
    record(phone, QStringLiteral("Sequence Out of Order"), 7, 9, 0);
    record(phone, QStringLiteral("Sequence Out of Order"), 7, 9, 20);
    record(phone, QStringLiteral("Sequence Out of Order"), 7, 9, 40);
    note(phone, QStringLiteral("Played Morning Medley"), 7, 9, 50);
    note(phone, QStringLiteral("Played Evening Song"), 7, 10, 0);
    note(phone, QStringLiteral("Visitor: Sam Example"), 7, 10, 10);
    record(phone, QStringLiteral("Mute Delay"), 7, 10, 20);
}

/// Ends the open log: shared at `hour:minute` of `day` when asked, and a new log started on `next_day` at 08:50 (the
/// facilitator carries over; the rest of the header is filled again).
void close_log(Phone& phone, int day, int hour, int minute, bool shared, int next_day)
{
    if (shared)
    {
        phone.services.go_to(day, hour, minute, 0);
        REQUIRE(phone.services.share().shareOpenLog());
        phone.services.settle();
    }
    phone.services.go_to(next_day, next_day == day ? 18 : 8, next_day == day ? 0 : 50);
    REQUIRE(phone.services.session().startNewLog());
    phone.services.settle();
}

/// The History and Past-log mock-ups' world. The open log is the one of 7 Nov, and the past logs are
///   2 Nov  UNIT-26  North Site  v1.4.0   5 entries   not shared
///   5 Nov  UNIT-27  North Site  v1.4.1  12 entries   shared 16:58
///   6 Nov  UNIT-27  North Site  v1.4.1   9 entries   shared 12:15
///   6 Nov  UNIT-27  South Site  v1.4.2   4 entries   shared 17:42
/// The scene starts on 2 Nov, so the first log is the one the app opened that day.
void history_day(Phone& phone)
{
    const QStringList cycle{QStringLiteral("Wrong Response"),     QStringLiteral("Track Skipped"),
                            QStringLiteral("Mute Delay"),         QStringLiteral("Wrong Track Played"),
                            QStringLiteral("Speech Delay"),       QStringLiteral("Track Not Found"),
                            QStringLiteral("Audio Drop Out"),     QStringLiteral("Volume Jump"),
                            QStringLiteral("Navigation Failure"), QStringLiteral("Track Delay")};
    const auto entries = [&](int day, int count, int first_hour, int first_minute)
    {
        for (int i = 0; i < count; ++i)
        {
            const int minutes = first_hour * 60 + first_minute + i * 37;
            record(phone, cycle[static_cast<qsizetype>(i % cycle.size())], day, minutes / 60, minutes % 60);
        }
    };

    fill_header(phone, QStringLiteral("26"), QStringLiteral("North Site"), QStringLiteral("1.4.0"));
    entries(2, 5, 9, 10);
    close_log(phone, 2, 0, 0, false, 5);

    fill_header(phone, QStringLiteral("27"), QStringLiteral("North Site"), QStringLiteral("1.4.1"));
    // The Past-log mock-up's first rows: 09:15 Wrong Response, 09:48 Track Skipped, a note at 10:30, 11:02 Mute Delay,
    // 13:20 Wrong Track Played, 15:44 Speech Delay.
    record(phone, QStringLiteral("Wrong Response"), 5, 9, 15);
    record(phone, QStringLiteral("Track Skipped"), 5, 9, 48);
    played_song(phone, QStringLiteral("Morning Medley"), 5, 10, 30);
    record(phone, QStringLiteral("Mute Delay"), 5, 11, 2);
    record(phone, QStringLiteral("Wrong Track Played"), 5, 13, 20);
    attach_note(phone, 4, QStringLiteral("asked for Evening Song"));
    record(phone, QStringLiteral("Speech Delay"), 5, 15, 44);
    record(phone, QStringLiteral("Track Not Found"), 5, 15, 50);
    record(phone, QStringLiteral("Audio Drop Out"), 5, 16, 5);
    record(phone, QStringLiteral("Volume Jump"), 5, 16, 20);
    record(phone, QStringLiteral("Track Delay"), 5, 16, 35);
    record(phone, QStringLiteral("Navigation Failure"), 5, 16, 40);
    note(phone, QStringLiteral("Played Evening Song"), 5, 16, 45);
    close_log(phone, 5, 16, 58, true, 6);

    fill_header(phone, QStringLiteral("27"), QStringLiteral("North Site"), QStringLiteral("1.4.1"));
    entries(6, 9, 8, 30);
    close_log(phone, 6, 12, 15, true, 6);

    fill_header(phone, QStringLiteral("27"), QStringLiteral("South Site"), QStringLiteral("1.4.2"));
    entries(6, 4, 13, 20);
    close_log(phone, 6, 17, 42, true, 7);

    fill_header(phone);
    phone.ui.settle();
}

/// A setup file the person is given: the sample setup, with its name for the confirmation.
QByteArray sample_setup_text()
{
    QFile file(QStringLiteral(EJ_SAMPLE_CONFIG));
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

/// A file the person chooses to import, in the store the rig reads files from, chosen through the stub chooser.
void choose_file_to_import(Phone& phone, const QByteArray& text, const QString& display_name)
{
    const QString path = QStringLiteral("imports/") + display_name;
    phone.services.memory().config().add_file(path.toStdString(), text.toStdString());
    REQUIRE(QMetaObject::invokeMethod(phone.chooser.get(), "openChosen", Q_ARG(QString, path),
                                      Q_ARG(QString, display_name)));
    phone.ui.settle();
    phone.play();
}

// ---- the scenes ----

/// One mock-up of docs/design/screens and the way to bring the app to its state. A null `m_Build` is a mock-up with no
/// state to capture, and `m_Note` says why; with a state, it says where the data or the state differs from the mock-up.
struct Scene
{
    QString m_Name;
    Start m_Start;
    std::function<void(Phone&)> m_Build;
    QString m_Note;
    Device m_Device = PHONE;
};

/// The Log tab with the day, as the first four Log mock-ups have it.
void log_with_day(Phone& phone)
{
    fill_header(phone);
    log_day(phone);
}

void open_details(Phone& phone)
{
    phone.tap(QStringLiteral("logHeaderCard"));
    phone.play();
}

void scene_main(Phone& phone)
{
    log_with_day(phone);
    // The toast of the last tap, as it is in its five seconds.
    phone.ui.advance(300ms);
}

void scene_log_banner(Phone& phone)
{
    log_day(phone);
    phone.services.advance(6s);
    phone.ui.advance(300ms);
}

void scene_recovery(Phone& phone)
{
    log_with_day(phone);
    phone.services.advance(6s);
    RecoveryReport report;
    report.m_Outcome = RecoveryOutcome::REBUILT;
    report.m_OpenLogEntries = 7;
    report.m_RebuiltRecords = 7;
    phone.relaunch([&] { phone.services.set_startup_recovery(report); });
    phone.ui.advance(300ms);
}

void scene_write_failed(Phone& phone)
{
    log_with_day(phone);
    phone.services.advance(6s);
    phone.services.repository().fail_next(RepositoryOp::ADD_ENTRY);
    REQUIRE(phone.services.tap(phone.type_id(QStringLiteral("Mute Delay"))));
    phone.services.settle();
    phone.ui.advance(300ms);
}

void scene_template_value(Phone& phone)
{
    log_with_day(phone);
    phone.services.go_to(7, 10, 30);
    phone.services.advance(6s);
    phone.ui.advance(300ms);
    phone.tap_item(phone.nth(QStringLiteral("chip"), 0));
    phone.play();
    phone.type_into(QStringLiteral("templateValueField"), QStringLiteral("Morning Medley"));
    phone.play();
}

void scene_tile_labels(Phone& phone)
{
    fill_header(phone);
    label_day(phone);
    phone.services.advance(6s);
    phone.ui.advance(300ms);
}

void scene_tile_labels_large(Phone& phone)
{
    scene_tile_labels(phone);
    phone.bridge.set_text_scale(1.3);
    phone.ui.advance(300ms);
}

void scene_other(Phone& phone)
{
    // Two types that are not on the board, as the Error types editor adds them: the picker lists them first.
    for (const QString& name : {QStringLiteral("Audio Echo"), QStringLiteral("Screen Freeze")})
    {
        const QString problem = phone.services.settings().addErrorType(name);
        INFO(problem.toStdString());
        REQUIRE(problem.isEmpty());
        phone.services.settle();
    }
    log_with_day(phone);
    phone.services.advance(6s);
    phone.tap(QStringLiteral("tile11"));
    phone.play();
}

void scene_new_day(Phone& phone)
{
    log_with_day(phone);
    phone.services.go_to(8, 8, 5);
    phone.services.day().check();
    phone.ui.settle();
    phone.play();
}

void scene_header(Phone& phone)
{
    // As the person fills it in, the unit number last, so that its field still has the focus. The version and the
    // location are below the foot bar when the page is at rest (the Environment row is above them), so the page is
    // scrolled to them, as a person scrolls, and back to rest for the picture.
    open_details(phone);
    QQuickItem* scroller = phone.need(QStringLiteral("headerScroller"));
    scroller->setProperty("contentY", scroller->property("contentHeight").toDouble() - scroller->height());
    phone.play();
    phone.type_into(QStringLiteral("headerVersion"), QStringLiteral("1.4.2"));
    phone.tap_item(phone.nth(QStringLiteral("headerLocation"), 0));
    phone.play();
    scroller->setProperty("contentY", -scroller->property("topMargin").toDouble());
    phone.play();
    phone.type_into(QStringLiteral("headerUnit"), QStringLiteral("27"));
    phone.play();
}

void scroll_details_to_end(Phone& phone);

void scene_change_version(Phone& phone)
{
    log_with_day(phone);
    phone.services.go_to(7, 13, 15);
    phone.services.advance(6s);
    open_details(phone);
    // As a person opens the version sheet now: the check mark of the Software version field is its button, and the
    // field is below the foot bar while the page is at rest, so the page is scrolled to it first.
    scroll_details_to_end(phone);
    phone.tap(QStringLiteral("formAccessory"), phone.need(QStringLiteral("headerVersion")));
    phone.play();
    phone.tap(QStringLiteral("versionChanged"));
    phone.play();
    phone.type_into(QStringLiteral("versionTo"), QStringLiteral("1.5.0"));
    phone.type_into(QStringLiteral("versionCommit"), QStringLiteral("e4f5a6b"));
    phone.play();
}

void scroll_details_to_end(Phone& phone)
{
    QQuickItem* scroller = phone.need(QStringLiteral("headerScroller"));
    const double content = scroller->property("contentHeight").toDouble();
    scroller->setProperty("contentY", content - scroller->height());
    // The title of the collapsed bar fades in over 240 ms; the picture is of the end of it.
    phone.play();
}

void scene_header_new_log_row(Phone& phone)
{
    log_with_day(phone);
    phone.services.advance(6s);
    open_details(phone);
    scroll_details_to_end(phone);
}

void scene_start_new_log_confirm(Phone& phone)
{
    scene_header_new_log_row(phone);
    phone.tap(QStringLiteral("headerNewLogRow"));
    phone.play();
}

void scene_review(Phone& phone)
{
    fill_header(phone);
    review_day(phone);
    phone.services.advance(6s);
    phone.choose_tab(REVIEW_TAB);
    phone.swipe_open(phone.nth(QStringLiteral("reviewSwipe"), 1));
}

void open_edit_entry(Phone& phone)
{
    fill_header(phone);
    review_day(phone);
    phone.services.advance(6s);
    phone.choose_tab(REVIEW_TAB);
    phone.tap_item(phone.nth(QStringLiteral("reviewSwipe"), 4));
    phone.play();
}

void scene_edit_entry(Phone& phone)
{
    open_edit_entry(phone);
}

void scene_time_tumbler(Phone& phone)
{
    open_edit_entry(phone);
    phone.tap(QStringLiteral("editTime"));
    phone.play();
}

void scene_time_tumbler_12(Phone& phone)
{
    fill_header(phone);
    review_day(phone);
    REQUIRE(phone.services.settings().setTimeFormat(QStringLiteral("12h"), true, QStringLiteral("upper")).isEmpty());
    phone.services.settle();
    phone.services.advance(6s);
    phone.choose_tab(REVIEW_TAB);
    phone.tap_item(phone.nth(QStringLiteral("reviewSwipe"), 4));
    phone.play();
    phone.tap(QStringLiteral("editTime"));
    phone.play();
}

void scene_history(Phone& phone)
{
    history_day(phone);
    phone.choose_tab(HISTORY_TAB);
    phone.swipe_open(phone.nth(QStringLiteral("historySwipe"), 2));
}

void scene_history_empty(Phone& phone)
{
    fill_header(phone);
    phone.choose_tab(HISTORY_TAB);
    phone.play();
}

void scene_past_log_review(Phone& phone)
{
    history_day(phone);
    phone.choose_tab(HISTORY_TAB);
    phone.tap_item(phone.nth(QStringLiteral("historySwipe"), 2));
    phone.ui.settle();
    phone.play();
}

void scene_delete_log(Phone& phone)
{
    history_day(phone);
    phone.choose_tab(HISTORY_TAB);
    phone.swipe_open(phone.nth(QStringLiteral("historySwipe"), 2));
    phone.tap(QStringLiteral("deleteAction"), phone.nth(QStringLiteral("historySwipe"), 2));
    phone.play();
}

void open_settings_page(Phone& phone, const QString& row);

/// The sample setup with only its first `kept` error types, of which the first `placed` are on the board: the setup of
/// a person who has started the board by hand. It is imported as the person imports a file.
void use_short_board(Phone& phone, int kept, int placed)
{
    QJsonObject setup = QJsonDocument::fromJson(sample_setup_text()).object();
    QJsonArray types;
    const QJsonArray all = setup.value(QStringLiteral("error_types")).toArray();
    for (int i = 0; i < kept && i < all.size(); ++i)
    {
        QJsonObject type = all.at(i).toObject();
        if (i >= placed)
        {
            type.remove(QStringLiteral("board_position"));
        }
        types.append(type);
    }
    setup.insert(QStringLiteral("error_types"), types);
    choose_file_to_import(phone, QJsonDocument(setup).toJson(), QStringLiteral("short-board.json"));
    // The confirmation, as the person answers it.
    phone.tap(QStringLiteral("replaceButton"));
    phone.services.settle();
    phone.play();
}

/// Four types on the board and a fifth, Track Skipped, off it: positions 5 to 11 are empty.
void board_settings_empty(Phone& phone)
{
    use_short_board(phone, 5, 4);
    phone.choose_tab(SETTINGS_TAB);
}

void scene_board_settings_empty(Phone& phone)
{
    board_settings_empty(phone);
}

void scene_board_type_picker(Phone& phone)
{
    board_settings_empty(phone);
    phone.tap(QStringLiteral("miniTile4"));
    phone.play();
}

void scene_error_type_new_on_board(Phone& phone)
{
    scene_board_type_picker(phone);
    phone.tap(QStringLiteral("settingsPickerNewType"));
    phone.play();
    phone.type_into(QStringLiteral("nameField"), QStringLiteral("Audio Drop Out"));
    phone.play();
}

void scene_error_types_added(Phone& phone)
{
    use_short_board(phone, 5, 4);
    open_settings_page(phone, QStringLiteral("settingsErrorTypes"));
    phone.tap(QStringLiteral("addRow"));
    phone.play();
    phone.type_into(QStringLiteral("nameField"), QStringLiteral("Audio Drop Out"));
    phone.tap(QStringLiteral("navTrailing"), phone.sheet_of(phone.need(QStringLiteral("nameField"))));
    phone.services.settle();
    phone.play();
}

void scene_log_empty_board(Phone& phone)
{
    // A setup made by hand: no error types yet.
    use_short_board(phone, 0, 0);
    fill_header(phone);
    phone.play();
}

void scene_board_settings(Phone& phone)
{
    phone.choose_tab(SETTINGS_TAB);
}

void scene_board_layout_selected(Phone& phone)
{
    phone.choose_tab(SETTINGS_TAB);
    phone.tap(QStringLiteral("miniTile8"));
    phone.play();
}

/// Scrolls the page that is in view until the item is, as the person does, and returns it.
QQuickItem* reveal(Phone& phone, QQuickItem* item)
{
    QQuickItem* flickable = phone.need(QStringLiteral("settingsFlickable"));
    auto* content = flickable->property("contentItem").value<QQuickItem*>();
    REQUIRE(content != nullptr);
    const double top = item->mapToItem(content, QPointF(0, 0)).y();
    const double most = std::max(0.0, flickable->property("contentHeight").toDouble() - flickable->height());
    flickable->setProperty("contentY", std::clamp(top - flickable->height() / 3, 0.0, most));
    phone.ui.layout();
    return item;
}

void open_settings_page(Phone& phone, const QString& row)
{
    phone.choose_tab(SETTINGS_TAB);
    // The list is longer than the screen, so a row far down is scrolled to first.
    phone.tap_item(reveal(phone, phone.need(row)));
    phone.play();
}

void scene_about(Phone& phone)
{
    phone.choose_tab(SETTINGS_TAB);
    phone.tap_item(reveal(phone, phone.need(QStringLiteral("settingsAboutRow"))));
    phone.play();
}

void scene_about_licence(Phone& phone)
{
    scene_about(phone);
    phone.tap_item(reveal(phone, phone.nth(QStringLiteral("aboutComponent"), 0)));
    phone.play();
}

void scene_error_types(Phone& phone)
{
    open_settings_page(phone, QStringLiteral("settingsErrorTypes"));
}

void scene_error_type_edit(Phone& phone)
{
    open_settings_page(phone, QStringLiteral("settingsErrorTypes"));
    // The row of "Track Delay" is the 10th of the board.
    phone.tap_item(phone.nth(QStringLiteral("typeRow"), 9));
    phone.play();
    QQuickItem* field = phone.need(QStringLiteral("nameField"));
    QQuickItem* input = phone.ui.find_item(QStringLiteral("formInput"), field);
    phone.ui.tap(input != nullptr ? input : field);
    for (int i = 0; i < 20; ++i)
    {
        phone.ui.key(Qt::Key_Backspace);
    }
    phone.ui.type(QStringLiteral("Mute Delay"));
    phone.play();
    // Save: the model refuses the duplicate name and the sheet says so.
    phone.tap(QStringLiteral("navTrailing"), phone.sheet_of(phone.need(QStringLiteral("nameField"))));
    phone.play();
}

void scene_locations(Phone& phone)
{
    open_settings_page(phone, QStringLiteral("settingsLocations"));
    phone.type_into(QStringLiteral("newLocationField"), QStringLiteral("East Depot"));
    phone.tap(QStringLiteral("addButton"));
    phone.play();
    phone.play();
    phone.type_into(QStringLiteral("newLocationField"), QStringLiteral("West Yard"));
    phone.play();
}

void scene_templates(Phone& phone)
{
    open_settings_page(phone, QStringLiteral("settingsTemplates"));
}

void scene_template_edit(Phone& phone)
{
    open_settings_page(phone, QStringLiteral("settingsTemplates"));
    phone.tap_item(phone.nth(QStringLiteral("templateRow"), 0));
    phone.play();
}

void scene_template_edit_invalid(Phone& phone)
{
    scene_template_edit(phone);
    QQuickItem* field = phone.need(QStringLiteral("patternField"));
    QQuickItem* input = phone.ui.find_item(QStringLiteral("formInput"), field);
    phone.ui.tap(input != nullptr ? input : field);
    phone.ui.key(Qt::Key_End);
    for (int i = 0; i < 8; ++i)
    {
        phone.ui.key(Qt::Key_Backspace);
    }
    phone.play();
}

void scene_import_export(Phone& phone)
{
    open_settings_page(phone, QStringLiteral("settingsImportExport"));
    phone.tap(QStringLiteral("settingsExport"));
    REQUIRE(QMetaObject::invokeMethod(phone.chooser.get(), "saveChosen",
                                      Q_ARG(QString, QStringLiteral("exports/error-journaling-setup.json"))));
    phone.ui.settle();
    phone.play();
}

void scene_import_confirm(Phone& phone)
{
    phone.choose_tab(SETTINGS_TAB);
    choose_file_to_import(phone, sample_setup_text(), QStringLiteral("error-journaling-setup.json"));
}

/// The sample setup with three faults: an error type with no name, two locations with one name, and a template pattern
/// with no {value}.
QByteArray faulty_setup_text()
{
    QJsonObject setup = QJsonDocument::fromJson(sample_setup_text()).object();
    QJsonArray types = setup.value(QStringLiteral("error_types")).toArray();
    QJsonObject third = types.at(2).toObject();
    third.insert(QStringLiteral("name"), QString());
    types.replace(2, third);
    setup.insert(QStringLiteral("error_types"), types);
    QJsonArray locations = setup.value(QStringLiteral("locations")).toArray();
    QJsonObject second = locations.at(1).toObject();
    second.insert(QStringLiteral("name"), locations.at(0).toObject().value(QStringLiteral("name")));
    locations.replace(1, second);
    setup.insert(QStringLiteral("locations"), locations);
    QJsonArray templates = setup.value(QStringLiteral("note_templates")).toArray();
    QJsonObject first = templates.at(0).toObject();
    first.insert(QStringLiteral("output_pattern"), QStringLiteral("Played"));
    templates.replace(0, first);
    setup.insert(QStringLiteral("note_templates"), templates);
    return QJsonDocument(setup).toJson();
}

void scene_import_errors(Phone& phone)
{
    phone.choose_tab(SETTINGS_TAB);
    choose_file_to_import(phone, faulty_setup_text(), QStringLiteral("error-journaling-setup.json"));
}

void scene_first_launch(Phone& phone)
{
    phone.ui.advance(100ms);
}

void scene_first_launch_manual(Phone& phone)
{
    phone.tap(QStringLiteral("manualCard"));
    phone.play();
    phone.type_into(QStringLiteral("facilitatorField"), QStringLiteral("Sam Example"));
    phone.play();
}

const Start TODAY;
const Start FIRST_LAUNCH{7, false};
const Start FOUR_DAYS_BACK{2, true};

/// Every phone mock-up of docs/design/screens, in the order of docs/design/README.md.
std::vector<Scene> phone_scenes()
{
    return {
        {QStringLiteral("Main"), TODAY, scene_main,
         QStringLiteral("Seven entries, the last Mute Delay at 10:20, with its toast.")},
        {QStringLiteral("Other"), TODAY, scene_other,
         QStringLiteral("The picker over the Log screen. Two types that are not on the board (Audio Echo, Screen "
                        "Freeze) were added in Settings so that \"Not on your board\" has rows; the mock-up's order of "
                        "the board's types is its own.")},
        {QStringLiteral("Header"), TODAY, scene_header,
         QStringLiteral("Filled in as the person does: version, location, then the unit number (27), whose field keeps "
                        "the focus. The mock-up's Date line has the year; the app's does not.")},
        {QStringLiteral("ChangeVersion"), TODAY, scene_change_version,
         QStringLiteral(
             "\"It changed during the day\", To typed 1.5.0 with the commit e4f5a6b, the time is now 13:15.")},
        {QStringLiteral("Review"), TODAY, scene_review,
         QStringLiteral("The mock-up's seven entries; the second row is swiped open.")},
        {QStringLiteral("EditEntry"), TODAY, scene_edit_entry,
         QStringLiteral("The 11:02 Wrong Track Played entry, with its note.")},
        {QStringLiteral("NewDay"), TODAY, scene_new_day,
         QStringLiteral(
             "The prompt on Sunday 8 Nov over the Log screen; the mock-up's backdrop is blocks, not the screen.")},
        {QStringLiteral("BoardSettings"), TODAY, scene_board_settings, QStringLiteral("The Settings tab at the top.")},
        {QStringLiteral("History"), FOUR_DAYS_BACK, scene_history,
         QStringLiteral("Four past logs as in the mock-up; the 5 Nov row is swiped open.")},
        {QStringLiteral("HistoryEmpty"), TODAY, scene_history_empty, QStringLiteral("No past logs.")},
        {QStringLiteral("PastLogReview"), FOUR_DAYS_BACK, scene_past_log_review,
         QStringLiteral("The 5 Nov log (12 entries, 6 shown in the mock-up); the list goes on below the first rows.")},
        {QStringLiteral("DeleteLog"), FOUR_DAYS_BACK, scene_delete_log,
         QStringLiteral("The alert for the 5 Nov log, over History.")},
        {QStringLiteral("LogBanner"), TODAY, scene_log_banner,
         QStringLiteral("Header fields empty, seven entries logged before it was filled (HDR-06).")},
        {QStringLiteral("TemplateValue"), TODAY, scene_template_value,
         QStringLiteral(
             "The \"Played song\" prompt with \"Morning Medley\" typed; there is no keyboard in the capture.")},
        {QStringLiteral("WriteFailed"), TODAY, scene_write_failed,
         QStringLiteral("A real failed commit of the next Mute Delay tap (the store is told to fail it).")},
        {QStringLiteral("Recovery"), TODAY, scene_recovery,
         QStringLiteral("A relaunch whose start-up recovery reports REBUILT; the app's own words for it.")},
        {QStringLiteral("HeaderNewLogRow"), TODAY, scene_header_new_log_row,
         QStringLiteral("Today's details scrolled to the end.")},
        {QStringLiteral("StartNewLogConfirm"), TODAY, scene_start_new_log_confirm,
         QStringLiteral("The alert over Today's details scrolled to the end.")},
        {QStringLiteral("TimeTumbler"), TODAY, scene_time_tumbler,
         QStringLiteral("The time page of Edit entry for the 11:02 entry.")},
        {QStringLiteral("TimeTumbler12"), TODAY, scene_time_tumbler_12,
         QStringLiteral("The time page of Edit entry with the 12-hour format and seconds; the entry is 11:02 AM, the "
                        "mock-up shows PM.")},
        {QStringLiteral("ErrorTypes"), TODAY, scene_error_types, QStringLiteral("Settings, Error types.")},
        {QStringLiteral("ErrorTypeEdit"), TODAY, scene_error_type_edit,
         QStringLiteral("Track Delay renamed to \"Mute Delay\" so that the duplicate refusal shows; its colour differs "
                        "from the mock-up's.")},
        {QStringLiteral("Locations"), TODAY, scene_locations,
         QStringLiteral("A third location, East Depot, added, and \"West Yard\" typed in the new-location field.")},
        {QStringLiteral("Templates"), TODAY, scene_templates,
         QStringLiteral("Settings, Note templates (the sample's two).")},
        {QStringLiteral("TemplateEdit"), TODAY, scene_template_edit,
         QStringLiteral("The first template of the sample setup.")},
        {QStringLiteral("TemplateEditInvalid"), TODAY, scene_template_edit_invalid,
         QStringLiteral("The line edited to \"Played \" (the answer deleted), which has no [answer].")},
        {QStringLiteral("FirstLaunch"), FIRST_LAUNCH, scene_first_launch, QStringLiteral("No setup imported yet.")},
        {QStringLiteral("FirstLaunchManual"), FIRST_LAUNCH, scene_first_launch_manual,
         QStringLiteral("\"Set up by hand\" chosen.")},
        {QStringLiteral("ImportExport"), TODAY, scene_import_export,
         QStringLiteral("Settings, Import or export, after an export to a path chosen in the (stub) file chooser.")},
        {QStringLiteral("ImportConfirm"), TODAY, scene_import_confirm,
         QStringLiteral("The sample setup chosen as error-journaling-setup.json, over Settings.")},
        {QStringLiteral("ImportErrors"), TODAY, scene_import_errors,
         QStringLiteral(
             "The sample setup with an unnamed error type, a repeated location name and a pattern with no {value}.")},
        {QStringLiteral("BoardLayoutSelected"), TODAY, scene_board_layout_selected,
         QStringLiteral("Position 9 (Speech Delay) selected.")},
        {QStringLiteral("LogEmptyBoard"), TODAY, scene_log_empty_board,
         QStringLiteral("The sample setup with no error types, the header filled in.")},
        {QStringLiteral("BoardSettingsEmpty"), TODAY, scene_board_settings_empty,
         QStringLiteral("The sample's first five types, four of them on the board.")},
        {QStringLiteral("BoardTypePicker"), TODAY, scene_board_type_picker,
         QStringLiteral("The empty position 5 tapped on that board.")},
        {QStringLiteral("ErrorTypeNewOnBoard"), TODAY, scene_error_type_new_on_board,
         QStringLiteral("New error type chosen in the picker, \"Audio Drop Out\" typed; there is no keyboard in the "
                        "capture, and the field shows its own focus.")},
        {QStringLiteral("ErrorTypesAdded"), TODAY, scene_error_types_added,
         QStringLiteral(
             "Audio Drop Out added in Error types on that board. The mock-up's list is the sample's; the app "
             "lists the six types it has.")},
        {QStringLiteral("TileLabels"), TODAY, scene_tile_labels,
         QStringLiteral("\"Sequence Out of Order\" logged three times; the mock-up's dashed design note is not UI and "
                        "is not in the app.")},
        {QStringLiteral("About"), TODAY, scene_about,
         QStringLiteral(
             "Settings, About CS Bingo Board, at the top. The mock-up's rows are the build of the mock-up (57, "
             "a1b2c3d, develop, 2026-11-09 03:21 UTC); the app's components are made up for the capture.")},
        {QStringLiteral("AboutLicence"), TODAY, scene_about_licence,
         QStringLiteral("The first component of the About list (Qt Base) opened; the made-up texts are short stand-ins "
                        "for the licences the mock-up begins.")},
        {QStringLiteral("TileLabelsLarge"), TODAY, scene_tile_labels_large,
         QStringLiteral("The same board at a text scale of 1.3 (the cap); the design note is not in the app.")},
    };
}

// ---- the states with no mock-up, for ej_screenshots (docs/plan/stage-7.md, 7.1) ----

void scene_tap_hint(Phone& phone)
{
    // One tap on a tile logs nothing (4.17a): the screen says to tap twice.
    log_with_day(phone);
    phone.services.advance(6s);
    phone.tap(QStringLiteral("tile0"));
    phone.ui.advance(300ms);
}

void scene_hold_note(Phone& phone)
{
    // A hold with an empty composer logs the error and opens Edit entry on it, in the note (BRD-07).
    log_with_day(phone);
    phone.services.advance(6s);
    phone.ui.hold(phone.need(QStringLiteral("tile0")), 700ms);
    phone.play();
}

void scene_other_search(Phone& phone, const QString& text)
{
    scene_other(phone);
    phone.type_into(QStringLiteral("pickerSearch"), text);
    phone.play();
}

void scene_environment_sheet(Phone& phone)
{
    log_with_day(phone);
    open_details(phone);
    phone.tap(QStringLiteral("headerEnvironment"));
    phone.play();
}

void scene_version_time(Phone& phone)
{
    scene_change_version(phone);
    phone.tap(QStringLiteral("versionTime"));
    phone.play();
}

/// The states that have no mock-up: sheets and pages that the design package did not draw.
std::vector<Scene> extra_scenes()
{
    return {
        {QStringLiteral("TapHint"), TODAY, scene_tap_hint, QStringLiteral("One tap on Wrong Response.")},
        {QStringLiteral("HoldNote"), TODAY, scene_hold_note,
         QStringLiteral("A hold on Wrong Response with an empty composer.")},
        {QStringLiteral("OtherSearch"), TODAY,
         [](Phone& phone) { scene_other_search(phone, QStringLiteral("Track")); },
         QStringLiteral("Other, searching for \"Track\".")},
        {QStringLiteral("OtherNoMatch"), TODAY,
         [](Phone& phone) { scene_other_search(phone, QStringLiteral("Zebra")); },
         QStringLiteral("Other, searching for a type that does not exist.")},
        {QStringLiteral("EnvironmentSheet"), TODAY, scene_environment_sheet,
         QStringLiteral("The Environment row of Today's details tapped.")},
        {QStringLiteral("VersionTime"), TODAY, scene_version_time,
         QStringLiteral("The time of the version change tapped.")},
        {QStringLiteral("Facilitator"), TODAY,
         [](Phone& phone) { open_settings_page(phone, QStringLiteral("settingsFacilitator")); },
         QStringLiteral("Settings, Facilitator (with the unit prefix and the totals line).")},
        {QStringLiteral("TimeFormat"), TODAY,
         [](Phone& phone) { open_settings_page(phone, QStringLiteral("settingsTimeFormat")); },
         QStringLiteral("Settings, Time format.")},
    };
}

/// The iPad mock-ups of Stage 8 (docs/plan/stage-8.md): nine of the phone's screens, each in portrait and landscape,
/// brought to the same state as on the phone.
const QStringList IPAD_SCREENS{
    QStringLiteral("Main"),          QStringLiteral("Review"),  QStringLiteral("Header"),
    QStringLiteral("BoardSettings"), QStringLiteral("History"), QStringLiteral("Other"),
    QStringLiteral("EditEntry"),     QStringLiteral("NewDay"),  QStringLiteral("FirstLaunch")};

/// Every mock-up: the phone's, then the iPad's.
std::vector<Scene> scenes()
{
    std::vector<Scene> all = phone_scenes();
    const std::vector<Scene> phone = all;
    for (const QString& name : IPAD_SCREENS)
    {
        const auto found =
            std::find_if(phone.begin(), phone.end(), [&name](const Scene& s) { return s.m_Name == name; });
        REQUIRE(found != phone.end());
        for (const Device& device : {IPAD_PORTRAIT, IPAD_LANDSCAPE})
        {
            Scene scene = *found;
            scene.m_Name = name + device.m_Suffix;
            scene.m_Device = device;
            scene.m_Note = QStringLiteral("The phone's scene in an iPad window. ") + found->m_Note;
            all.push_back(scene);
        }
    }
    return all;
}

QString capture_dir()
{
    return qEnvironmentVariable("EJ_CAPTURE_DIR");
}

bool wanted(const QString& name)
{
    const QString only = qEnvironmentVariable("EJ_CAPTURE_ONLY");
    if (only.isEmpty())
    {
        return true;
    }
    return only.split(QLatin1Char(','), Qt::SkipEmptyParts).contains(name);
}

/// Records what happened to a mock-up in capture-manifest.json (merged with what is already there).
void write_manifest(const QString& dir, const QString& name, bool captured, const QString& note)
{
    const QString path = QDir(dir).filePath(QStringLiteral("capture-manifest.json"));
    QJsonObject all;
    QFile in(path);
    if (in.open(QIODevice::ReadOnly))
    {
        all = QJsonDocument::fromJson(in.readAll()).object();
    }
    all.insert(name, QJsonObject{{QStringLiteral("captured"), captured}, {QStringLiteral("note"), note}});
    QFile out(path);
    REQUIRE(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
    out.write(QJsonDocument(all).toJson(QJsonDocument::Indented));
}

} // namespace

TEST_CASE("capture.mockups: render each state that has a mock-up, in light and dark", "[.capture]")
{
    const QString dir = capture_dir();
    if (dir.isEmpty())
    {
        SKIP("EJ_CAPTURE_DIR is not set");
    }
    REQUIRE(QDir().mkpath(dir));
    static const QString family = register_fonts();
    INFO("font family: " << family.toStdString());

    const std::vector<Scene> all = scenes();
    const Scene& scene = all[static_cast<std::size_t>(GENERATE_COPY(range(0, static_cast<int>(all.size()))))];
    CAPTURE(scene.m_Name);
    if (!wanted(scene.m_Name))
    {
        SUCCEED("not in EJ_CAPTURE_ONLY");
        return;
    }
    if (!scene.m_Build)
    {
        write_manifest(dir, scene.m_Name, false, scene.m_Note);
        SUCCEED("no app state");
        return;
    }

    Phone phone(scene.m_Start, scene.m_Device);
    scene.m_Build(phone);
    for (const bool dark : {false, true})
    {
        phone.ui.set_dark(dark);
        phone.ui.advance(50ms);
        // The software renderer paints the rows of a list that lie under the tab bar over it (see UiRig::repaint_over).
        phone.ui.repaint_over(phone.ui.find_item(QStringLiteral("shellTabBar")));
        const QImage image = phone.ui.grab();
        REQUIRE_FALSE(image.isNull());
        const QString file = QDir(dir).filePath(scene.m_Name + (dark ? QStringLiteral(".dark") : QString()) +
                                                QStringLiteral(".app.png"));
        REQUIRE(image.save(file));
    }
    write_manifest(dir, scene.m_Name, true, scene.m_Note);
}

// ---- ej_screenshots (docs/plan/stage-7.md, 7.1) ----

namespace
{

/// tools/ej_screenshots/screens.json.
QJsonObject screen_list()
{
    QFile file(QStringLiteral(EJ_SCREENS_JSON));
    REQUIRE(file.open(QIODevice::ReadOnly));
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll());
    REQUIRE(document.isObject());
    return document.object();
}

/// Every scene the program can build: the mock-ups (phone and iPad) and the states with no mock-up.
std::vector<Scene> screenshot_scenes()
{
    std::vector<Scene> all = scenes();
    for (Scene& scene : extra_scenes())
    {
        all.push_back(std::move(scene));
    }
    return all;
}

/// Whether a sheet or an alert is open anywhere under the item.
bool modal_open(const QQuickItem* item)
{
    if (item == nullptr)
    {
        return false;
    }
    if (item->property("modalOpen").toBool())
    {
        return true;
    }
    const QList<QQuickItem*> children = item->childItems();
    return std::any_of(children.begin(), children.end(), [](const QQuickItem* child) { return modal_open(child); });
}

} // namespace

TEST_CASE("screenshots.render: every state of screens.json renders in every mode, and every screen file has a state",
          "[ui][screenshots]")
{
    const QJsonObject list = screen_list();
    const std::vector<Scene> all = screenshot_scenes();

    // The list and the program agree: each state has a scene, each scene is listed once, and each screen file is
    // named by a state (or said not to be a screen), so a new screen cannot ship without a picture.
    QStringList listed;
    QStringList files;
    for (const QJsonValue flow : list.value(QStringLiteral("flows")).toArray())
    {
        for (const QJsonValue state : flow.toObject().value(QStringLiteral("states")).toArray())
        {
            listed.append(state.toObject().value(QStringLiteral("name")).toString());
            for (const QJsonValue file : state.toObject().value(QStringLiteral("files")).toArray())
            {
                files.append(file.toString());
            }
        }
    }
    QStringList built;
    for (const Scene& scene : all)
    {
        if (scene.m_Build)
        {
            built.append(scene.m_Name);
        }
    }
    for (const QString& name : listed)
    {
        CAPTURE(name.toStdString());
        CHECK(listed.count(name) == 1);
        CHECK(built.contains(name));
    }
    for (const QString& name : built)
    {
        CAPTURE(name.toStdString());
        CHECK(listed.contains(name));
    }
    const QJsonObject not_screens = list.value(QStringLiteral("not_screens")).toObject();
    const QDir root(QStringLiteral(EJ_QML_SCREENS_DIR));
    QDirIterator it(root.path(), {QStringLiteral("*.qml")}, QDir::Files, QDirIterator::Subdirectories);
    int screen_files = 0;
    while (it.hasNext())
    {
        const QString relative = root.relativeFilePath(it.next());
        if (relative.contains(QStringLiteral("private/")) || relative.startsWith(QStringLiteral("launch/")))
        {
            continue;
        }
        ++screen_files;
        CAPTURE(relative.toStdString());
        CHECK((files.contains(relative) || not_screens.contains(relative)));
    }
    CHECK(screen_files > 0);
    for (const QString& file : files)
    {
        CAPTURE(file.toStdString());
        CHECK(root.exists(file));
    }

    // The pictures: into EJ_SCREENSHOT_DIR when it is set, and into a folder that is thrown away when it is not, so a
    // ctest run checks that every state renders. EJ_SCREENSHOT_ONLY narrows the run to a comma separated list of states.
    QTemporaryDir scratch;
    QString dir = qEnvironmentVariable("EJ_SCREENSHOT_DIR");
    if (dir.isEmpty())
    {
        REQUIRE(scratch.isValid());
        dir = scratch.path();
    }
    REQUIRE(QDir().mkpath(dir));
    static const QString family = register_fonts();
    INFO("font family: " << family.toStdString());
    const QStringList only = qEnvironmentVariable("EJ_SCREENSHOT_ONLY").split(QLatin1Char(','), Qt::SkipEmptyParts);
    const QJsonArray modes = list.value(QStringLiteral("modes")).toArray();
    REQUIRE_FALSE(modes.isEmpty());

    QJsonArray written;
    for (const Scene& scene : all)
    {
        if (!scene.m_Build || !listed.contains(scene.m_Name) || (!only.isEmpty() && !only.contains(scene.m_Name)))
        {
            continue;
        }
        CAPTURE(scene.m_Name.toStdString());
        Phone phone(scene.m_Start, scene.m_Device);
        scene.m_Build(phone);
        for (const QJsonValue value : modes)
        {
            const QJsonObject mode = value.toObject();
            const QString mode_name = mode.value(QStringLiteral("name")).toString();
            CAPTURE(mode_name.toStdString());
            phone.ui.set_dark(mode.value(QStringLiteral("dark")).toBool());
            phone.bridge.set_text_scale(mode.value(QStringLiteral("text_scale")).toDouble(1.0));
            phone.ui.advance(300ms);
            // The software renderer paints the rows of a list that lie under the tab bar over it, so the tab bar is
            // painted again; not while a sheet or an alert is open, which the repaint would cover.
            if (!modal_open(phone.ui.root()))
            {
                phone.ui.repaint_over(phone.ui.find_item(QStringLiteral("shellTabBar")));
            }
            const QImage image = phone.ui.grab();
            REQUIRE_FALSE(image.isNull());
            const QString file = scene.m_Name + QLatin1Char('.') + mode_name + QStringLiteral(".png");
            REQUIRE(image.save(QDir(dir).filePath(file)));
            written.append(QJsonObject{{QStringLiteral("state"), scene.m_Name},
                                       {QStringLiteral("mode"), mode_name},
                                       {QStringLiteral("file"), file},
                                       {QStringLiteral("note"), scene.m_Note}});
        }
    }
    QFile manifest(QDir(dir).filePath(QStringLiteral("screenshots.json")));
    REQUIRE(manifest.open(QIODevice::WriteOnly | QIODevice::Truncate));
    manifest.write(QJsonDocument(written).toJson(QJsonDocument::Indented));
    if (only.isEmpty())
    {
        CHECK(written.size() == listed.size() * modes.size());
    }
}

#include "visual_capture_test.moc"
