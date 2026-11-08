// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The project and Stage 3 headers come before any Qt header: the headers of the core define a name (`STRICT`) that the
// Windows headers behind Qt Quick define as a macro.
#include "error_journaling/configuration.hpp"
#include "error_journaling/entry.hpp"
#include "error_journaling/header_values.hpp"
#include "error_journaling/ids.hpp"
#include "error_journaling/time_zone_provider.hpp"
#include "fake_time_zone_provider.hpp"
#include "golden_scenarios.hpp"
#include "rig/ui_rig.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_platform.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/sequential_ids.hpp"
#include "support/test_clock.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/environment.hpp"
#include "viewmodel/error_picker_model.hpp"
#include "viewmodel/log_document.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/share_service.hpp"
#include "viewmodel/timeline_model.hpp"
#include "viewmodel/undo_controller.hpp"
// Brings Qt Quick in, so it comes after the headers of the core.
#include "input_components_support.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QString>
#include <QTextDocumentFragment>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// What the tests of the Review screen and the Edit entry sheet (package 4.10) share: the real Stage 3 view models over
/// the in-memory store, the manual gateway and the fakes of the platform, with a log in the open slot and, when a test
/// asks, a past log. A test drives the models and loads the screen with `properties()`.
namespace ej::ui::review
{

/// Melbourne with its two changes of 2026, for the tests of the skipped and the repeated hour: daylight time (+11:00)
/// until 2026-04-04 16:00 UTC, standard time (+10:00) until 2026-10-03 16:00 UTC, daylight time again after that. So
/// 2026-10-04 has 23 hours (02:00 to 03:00 does not exist) and 2026-04-05 has 25 (02:00 to 03:00 happens twice). A
/// local time in the repeated hour resolves to the earlier instant, one in the gap to the instant after the gap, as the
/// provider contract of Stage 1 says and the Qt provider does.
class MelbourneDstZones final : public TimeZoneProvider
{
public:
    static constexpr auto ZONE = "Australia/Melbourne";

    [[nodiscard]] Result<LocalTime, Error> to_local(const Timestamp& timestamp) const override
    {
        if (timestamp.m_Zone != ZONE)
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const auto local = std::chrono::floor<std::chrono::seconds>(timestamp.m_Utc + offset_at(timestamp.m_Utc));
        const auto days = std::chrono::floor<std::chrono::days>(local);
        return LocalTime{std::chrono::year_month_day{days}, local - days};
    }

    [[nodiscard]] Result<UtcMillis, Error> to_utc(const LocalTime& local, std::string_view zone) const override
    {
        using namespace std::chrono_literals;
        if (zone != ZONE)
        {
            return fail(Error::UNKNOWN_ZONE);
        }
        const std::chrono::sys_days days{local.m_Date};
        std::optional<UtcMillis> earliest;
        for (const auto offset : {std::chrono::minutes{660}, std::chrono::minutes{600}})
        {
            const UtcMillis candidate{
                std::chrono::floor<std::chrono::milliseconds>(days + local.m_SinceMidnight - offset)};
            if (offset_at(candidate) == offset && (!earliest || candidate < *earliest))
            {
                earliest = candidate;
            }
        }
        if (earliest)
        {
            return *earliest;
        }
        // In the gap: the offset in force before the change, which is the instant after the gap.
        return UtcMillis{std::chrono::floor<std::chrono::milliseconds>(days + local.m_SinceMidnight - 600min)};
    }

private:
    static std::chrono::minutes offset_at(UtcMillis instant)
    {
        using namespace std::chrono_literals;
        if (instant < test::utc(2026, 4, 4, 16, 0))
        {
            return 660min;
        }
        return instant < test::utc(2026, 10, 3, 16, 0) ? 600min : 660min;
    }
};

[[nodiscard]] inline ErrorTypeId type_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_ErrorTypes, name, &ErrorType::m_Name);
    REQUIRE(it != configuration.m_ErrorTypes.end());
    return it->m_Id;
}

[[nodiscard]] inline QString id_text(std::uint64_t entry_number)
{
    return QString::fromStdString(test::sample_id<EntryId>(entry_number).format());
}

/// An error entry on 7 Nov 2026 at `hour:minute` in Melbourne.
[[nodiscard]] inline Entry error_at(const Configuration& configuration, std::uint64_t number, int hour, int minute,
                                    std::string label = "Wrong Response",
                                    std::optional<std::string> note = std::nullopt)
{
    Entry entry;
    entry.m_Id = test::sample_id<EntryId>(number);
    entry.m_Captured = test::on_day(hour, minute);
    entry.m_Body = ErrorEntry{type_named(configuration, label), std::move(note), std::move(label)};
    return entry;
}

[[nodiscard]] inline Entry note_at(std::uint64_t number, int hour, int minute, std::string text)
{
    Entry entry;
    entry.m_Id = test::sample_id<EntryId>(number);
    entry.m_Captured = test::on_day(hour, minute);
    entry.m_Body = NoteEntry{std::nullopt, std::move(text)};
    return entry;
}

[[nodiscard]] inline Entry change_at(std::uint64_t number, int hour, int minute, HeaderField field, std::string value)
{
    Entry entry;
    entry.m_Id = test::sample_id<EntryId>(number);
    entry.m_Captured = test::on_day(hour, minute);
    entry.m_Body = HeaderChangeEntry{field, std::move(value)};
    return entry;
}

/// The log of the PRD example (golden file `prd-example`): entries 1 to 6 on 7 Nov 2026.
[[nodiscard]] inline std::vector<Entry> prd_example(const Configuration& configuration)
{
    return {error_at(configuration, 1, 10, 5),
            error_at(configuration, 2, 10, 20, "Mute Delay"),
            note_at(3, 10, 30, "Played Morning Medley"),
            error_at(configuration, 4, 11, 2, "Wrong Track Played", "asked for Evening Song"),
            change_at(5, 13, 15, HeaderField::SOFTWARE_VERSION, "1.5.0"),
            error_at(configuration, 6, 13, 40, "Speech Delay")};
}

/// The lines of the golden file `prd-example`, as the Review screen shows them (`timeText + " " + lineText`).
[[nodiscard]] inline std::vector<QString> prd_example_lines()
{
    return {QStringLiteral("10:05 T| Wrong Response"),
            QStringLiteral("10:20 T| Mute Delay"),
            QStringLiteral("10:30 (Free Text) Played Morning Medley"),
            QStringLiteral("11:02 T| Wrong Track Played – asked for Evening Song"),
            QStringLiteral("13:15 Software Version changed: 1.4.2 → 1.5.0"),
            QStringLiteral("13:40 T| Speech Delay")};
}

/// The models of the screens over one in-memory store. The destruction order is the reverse of the declaration, so the
/// models, which listen to the sources, go first.
class World
{
public:
    static constexpr std::uint64_t PAST_LOG = 1;
    static constexpr std::uint64_t OPEN_LOG = 2;

    explicit World(const TimeZoneProvider* zones = nullptr, Configuration configuration = test::sample_configuration(),
                   bool share_available = true)
        : m_Zones(zones != nullptr ? zones : &m_Fixed), m_Configuration(configuration),
          m_Settings(m_Gateway, environment()), m_Registry(environment(), m_Gateway),
          m_Timeline(environment(), m_Settings, m_Clipboard), m_Undo(environment(), m_Settings),
          m_Picker(m_Settings, m_Open, m_Settings),
          m_Share(environment(), m_Open, m_Registry, m_Settings, m_Platform, share_available)
    {
        m_Storage.config().set_saved(export_configuration(configuration));
        m_Settings.load();
        m_Gateway.run_all();
        REQUIRE(m_Settings.configured());
        m_Timeline.follow(m_Open);
    }

    World(const World&) = delete;
    World& operator=(const World&) = delete;

    ~World()
    {
        if (m_Document)
        {
            m_Undo.stop_observing(*m_Document);
        }
        if (m_PastTimeline)
        {
            m_PastTimeline->unbind();
        }
        m_Timeline.unbind();
        m_Open.set(nullptr);
    }

    [[nodiscard]] viewmodel::Environment environment()
    {
        return viewmodel::Environment{m_Clock, m_Clock, m_Ids, m_Device, *m_Zones};
    }

    /// The log of the open slot: `entries` in a log with the standard header (7 Nov 2026, UNIT-27, version 1.4.2).
    void open_log(const std::vector<Entry>& entries, HeaderValues header = test::sample_header())
    {
        const LogId id = test::sample_id<LogId>(OPEN_LOG);
        REQUIRE(m_Storage.memory().create_log(id, header));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        m_Document = m_Registry.make_document(std::move(*loaded));
        m_Undo.observe(*m_Document);
        m_Open.set(m_Document.get());
    }

    void open_prd_example()
    {
        open_log(prd_example(m_Configuration));
    }

    /// An archived log (Thu 5 Nov in the design) shown by a second timeline, the one History's `reviewModel` is. Make
    /// it before the open log: the store holds one open log at a time.
    viewmodel::TimelineModel& make_past(const std::vector<Entry>& entries, HeaderValues header = test::sample_header())
    {
        const LogId id = test::sample_id<LogId>(PAST_LOG);
        REQUIRE(m_Storage.memory().create_log(id, header));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        REQUIRE(m_Storage.memory().archive_log(id));
        auto loaded = m_Storage.memory().load_log(id);
        REQUIRE(loaded.has_value());
        m_PastDocument = std::make_shared<viewmodel::LogDocument>(environment(), m_Gateway, std::move(*loaded));
        m_PastDocument->set_archived();
        m_PastTimeline = std::make_unique<viewmodel::TimelineModel>(environment(), m_Settings, m_Clipboard);
        m_PastTimeline->bind(m_PastDocument);
        return *m_PastTimeline;
    }

    /// The models as the properties the screens take.
    [[nodiscard]] QVariantMap properties(bool past = false)
    {
        QObject* timeline = past ? static_cast<QObject*>(m_PastTimeline.get()) : static_cast<QObject*>(&m_Timeline);
        return {{QStringLiteral("timeline"), QVariant::fromValue(timeline)},
                {QStringLiteral("share"), QVariant::fromValue(static_cast<QObject*>(&m_Share))},
                {QStringLiteral("picker"), QVariant::fromValue(static_cast<QObject*>(&m_Picker))},
                {QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject*>(&m_Settings))}};
    }

    void commit()
    {
        m_Gateway.run_all();
    }

    [[nodiscard]] viewmodel::TimelineModel& timeline()
    {
        return m_Timeline;
    }
    [[nodiscard]] viewmodel::UndoController& undo()
    {
        return m_Undo;
    }
    [[nodiscard]] viewmodel::ShareService& share()
    {
        return m_Share;
    }
    [[nodiscard]] viewmodel::SettingsModel& settings()
    {
        return m_Settings;
    }
    [[nodiscard]] test::FakePlatform& platform()
    {
        return m_Platform;
    }
    [[nodiscard]] test::FakeClipboard& clipboard()
    {
        return m_Clipboard;
    }
    [[nodiscard]] test::TestClock& clock()
    {
        return m_Clock;
    }
    [[nodiscard]] test::ManualStorageGateway& gateway()
    {
        return m_Gateway;
    }
    [[nodiscard]] test::MemoryStorage& storage()
    {
        return m_Storage;
    }
    [[nodiscard]] const Configuration& configuration() const
    {
        return m_Configuration;
    }

private:
    test::TestClock m_Clock;
    test::SequentialIds m_Ids;
    test::FakeDeviceZone m_Device;
    test::FakeTimeZoneProvider m_Fixed = test::make_fixed_zones();
    const TimeZoneProvider* m_Zones;
    Configuration m_Configuration;
    test::EventLog m_Events;
    test::MemoryStorage m_Storage{m_Clock, &m_Events};
    test::ManualStorageGateway m_Gateway{m_Storage.context()};
    test::FakeClipboard m_Clipboard{&m_Events};
    test::FakePlatform m_Platform{&m_Events};
    test::FakeOpenLogSource m_Open;
    viewmodel::SettingsModel m_Settings;
    viewmodel::DocumentRegistry m_Registry;
    std::shared_ptr<viewmodel::LogDocument> m_Document;
    std::shared_ptr<viewmodel::LogDocument> m_PastDocument;
    viewmodel::TimelineModel m_Timeline;
    viewmodel::UndoController m_Undo;
    viewmodel::ErrorPickerModel m_Picker;
    viewmodel::ShareService m_Share;
    std::unique_ptr<viewmodel::TimelineModel> m_PastTimeline;
};

// ---- looking at the screen ----

/// Every item under `from` (the loaded item by default) whose objectName is `name`, in tree order. The delegates of a
/// ListView are not children of the QObject tree, so `UiRig::find` does not reach them; the item tree does.
[[nodiscard]] inline std::vector<QQuickItem*> all_named(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    std::vector<QQuickItem*> found;
    for (const Node& node : rig.tree(from))
    {
        if (node.m_ObjectName == name)
        {
            found.push_back(node.m_Item);
        }
    }
    return found;
}

/// The first item named `name` under `from` that is shown, or the first one at all, or null.
[[nodiscard]] inline QQuickItem* find_in(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    QQuickItem* first = nullptr;
    for (const Node& node : rig.tree(from))
    {
        if (node.m_ObjectName != name)
        {
            continue;
        }
        if (node.m_Visible)
        {
            return node.m_Item;
        }
        if (first == nullptr)
        {
            first = node.m_Item;
        }
    }
    return first;
}

/// `find_in` for an item that must be there: the test stops with a failure when there is none.
[[nodiscard]] inline QQuickItem* require_in(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    QQuickItem* item = find_in(rig, name, from);
    REQUIRE(item != nullptr);
    return item;
}

[[nodiscard]] inline QString text_in(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    QQuickItem* item = find_in(rig, name, from);
    return item != nullptr ? item->property("text").toString() : QString();
}

/// The delegates of the list that are in use, in the order of the log. A ListView that reuses its delegates keeps the
/// ones it does not need in a pool, culled but still in the tree, so a row that left the model is not found by name;
/// the view says which delegate shows which row.
[[nodiscard]] inline std::vector<QQuickItem*> delegates(UiRig& rig)
{
    std::vector<QQuickItem*> out;
    QQuickItem* list = find_in(rig, QStringLiteral("reviewList"));
    if (list == nullptr)
    {
        return out;
    }
    const int count = list->property("count").toInt();
    for (int i = 0; i < count; ++i)
    {
        QQuickItem* item = nullptr;
        QMetaObject::invokeMethod(list, "itemAtIndex", Q_RETURN_ARG(QQuickItem*, item), Q_ARG(int, i));
        if (item != nullptr)
        {
            out.push_back(item);
        }
    }
    return out;
}

/// The words of a Text, whatever its format: a line with a prefix is StyledText (the prefix is in the secondary
/// colour), and what a person reads is the text without its markup.
[[nodiscard]] inline QString words_of(QQuickItem* item)
{
    if (item == nullptr)
    {
        return {};
    }
    const QString text = item->property("text").toString();
    constexpr int STYLED_TEXT = 4; // Text.StyledText
    return item->property("textFormat").toInt() == STYLED_TEXT ? QTextDocumentFragment::fromHtml(text).toPlainText()
                                                               : text;
}

/// The rows of the timeline as the screen shows them: the time and the line, joined by a space.
[[nodiscard]] inline std::vector<QString> row_lines(UiRig& rig)
{
    std::vector<QString> out;
    for (QQuickItem* delegate : delegates(rig))
    {
        out.push_back(text_in(rig, QStringLiteral("reviewTime"), delegate) + QLatin1Char(' ') +
                      words_of(find_in(rig, QStringLiteral("reviewLine"), delegate)));
    }
    return out;
}

/// The lines on separate lines, so that a failed comparison prints them readably.
[[nodiscard]] inline std::string joined(const std::vector<QString>& lines)
{
    std::string text;
    for (const QString& line : lines)
    {
        text += line.toStdString() + '\n';
    }
    return text;
}

/// The swipe rows of the list, top to bottom.
[[nodiscard]] inline std::vector<QQuickItem*> swipe_rows(UiRig& rig)
{
    std::vector<QQuickItem*> out;
    for (QQuickItem* delegate : delegates(rig))
    {
        out.push_back(find_in(rig, QStringLiteral("reviewSwipe"), delegate));
    }
    return out;
}

// ---- the screen over the models ----

/// How a test wants the world and the screen.
struct Options
{
    bool share_available = true;
    /// A past log shown by a second timeline, and the screen made for it.
    bool past = false;
    const TimeZoneProvider* zones = nullptr;
    /// The entries of the open log; none given = the PRD example.
    std::optional<std::vector<Entry>> entries;
    HeaderValues header = test::sample_header();
    /// The clock format of the configuration; none given = the sample's (24-hour).
    std::optional<TimeFormat> time_format;

    [[nodiscard]] static Options no_share_sheet()
    {
        Options options;
        options.share_available = false;
        return options;
    }
    [[nodiscard]] static Options empty_log()
    {
        Options options;
        options.entries = std::vector<Entry>{};
        return options;
    }
    [[nodiscard]] static Options past_log()
    {
        Options options;
        options.past = true;
        return options;
    }
};

/// The real view models with a log in the open slot, and the Review screen over them. The rig is declared after the
/// world, so it goes first when this is destroyed and no item outlives a model it reads.
struct Screen
{
    explicit Screen(Options options = {}) : m_World(options.zones, configured(options), options.share_available)
    {
        if (options.past)
        {
            m_World.make_past(prd_example(m_World.configuration()));
        }
        m_World.open_log(options.entries ? *options.entries : prd_example(m_World.configuration()), options.header);
        m_Rig.attach(m_World.clock());
        m_Rig.attach(m_World.gateway());
        QVariantMap properties = m_World.properties(options.past);
        if (options.past)
        {
            properties.insert(QStringLiteral("pastLog"), true);
            properties.insert(QStringLiteral("logId"),
                              QString::fromStdString(test::sample_id<LogId>(World::PAST_LOG).format()));
            properties.insert(QStringLiteral("title"), QStringLiteral("Thu 5 Nov"));
            properties.insert(QStringLiteral("sharedDateText"), QStringLiteral("5 Nov"));
            properties.insert(QStringLiteral("sharedTimeText"), QStringLiteral("16:58"));
        }
        m_Review = m_Rig.load(QStringLiteral("Review"), properties);
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Review != nullptr);
    }

    /// The sample configuration with the clock format of the options.
    [[nodiscard]] static Configuration configured(const Options& options)
    {
        Configuration configuration = test::sample_configuration();
        if (options.time_format)
        {
            configuration.m_TimeFormat = *options.time_format;
        }
        return configuration;
    }

    World m_World;
    UiRig m_Rig;
    QQuickItem* m_Review = nullptr;

    [[nodiscard]] QQuickItem* named(const char* name, QQuickItem* from = nullptr)
    {
        return find_in(m_Rig, QString::fromLatin1(name), from);
    }

    /// An item that must be there.
    [[nodiscard]] QQuickItem* item(const char* name, QQuickItem* from = nullptr)
    {
        return require_in(m_Rig, QString::fromLatin1(name), from);
    }

    [[nodiscard]] QQuickItem* row(int index)
    {
        const std::vector<QQuickItem*> rows = swipe_rows(m_Rig);
        REQUIRE(static_cast<int>(rows.size()) > index);
        return rows[static_cast<std::size_t>(index)];
    }

    [[nodiscard]] QQuickItem* face(int index)
    {
        QQuickItem* face = named("face", row(index));
        REQUIRE(face != nullptr);
        return face;
    }

    /// A swipe to the left on a row, which opens its actions.
    void swipe_open(int index)
    {
        QQuickItem* f = face(index);
        m_Rig.swipe(f, QPointF(f->width() - 60, f->height() / 2), QPointF(60, f->height() / 2));
        m_Rig.advance(std::chrono::milliseconds(400));
    }

    void tap_action(int index, const char* action)
    {
        swipe_open(index);
        QQuickItem* button = named(action, row(index));
        REQUIRE(button != nullptr);
        m_Rig.tap(button);
    }

    /// Runs the manual gateway, so that what was submitted is committed, and plays a little time.
    void commit()
    {
        m_Rig.settle();
        m_Rig.advance(std::chrono::milliseconds(100));
    }

    [[nodiscard]] bool visible(const char* name, QQuickItem* from = nullptr)
    {
        QQuickItem* item = named(name, from);
        return item != nullptr && m_Rig.geometry(item).height() > 0 && item->isVisible();
    }
};

} // namespace ej::ui::review
