// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// The project and Stage 3 headers come before any Qt header; review_test_support.hpp keeps that order. Only QString,
// which does not bring the Windows headers in, comes first: Catch2 prints a QString as a list of numbers, and the tests
// of this package compare many, so the specialisation must be seen before any header that compares one.
#include <catch2/catch_tostring.hpp>

#include <QString>
#include <string>

template <>
struct Catch::StringMaker<QString>
{
    static std::string convert(const QString& text)
    {
        return '"' + text.toStdString() + '"';
    }
};

#include "review_test_support.hpp"
#include "support/fake_sources.hpp"
#include "support/recording_repository.hpp"
#include "viewmodel/day_watcher.hpp"
#include "viewmodel/history_model.hpp"
#include "viewmodel/log_session.hpp"

#include <QMetaObject>
#include <QQuickItem>
#include <QString>
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

/// What the tests of the new-day prompt and the History screen (package 4.11) share: the real Stage 3 view models over
/// the in-memory store, the manual gateway and the fakes of the platform, in the way the tests of the view models build
/// them.
namespace ej::ui::history
{

using review::all_named;
using review::find_in;
using review::text_in;

/// A local time in Melbourne on `day` of November 2026 (+11:00 on every day of the tests that use it).
[[nodiscard]] inline UtcMillis melbourne(unsigned day, int hour, int minute)
{
    return test::utc(2026, 11, day, 0, 0) + std::chrono::hours{hour - 11} + std::chrono::minutes{minute};
}

[[nodiscard]] inline QString log_text(std::uint64_t log_number)
{
    return QString::fromStdString(test::sample_id<LogId>(log_number).format());
}

[[nodiscard]] inline LocationId location_named(const Configuration& configuration, std::string_view name)
{
    const auto it = std::ranges::find(configuration.m_Locations, name, &Location::m_Name);
    REQUIRE(it != configuration.m_Locations.end());
    return it->m_Id;
}

/// The History screen's world: four archived logs and the models over them. The destruction order is the reverse of
/// the declaration, so the models, which listen to the sources, go first, and the screen (declared after the world by
/// the test) before them.
///
///   LAST_YEAR   7 Nov 2025, North Site, 2 entries, not shared
///   YESTERDAY   6 Nov 2026, a typed location, 1 entry, not shared
///   MORNING     7 Nov 2026, North Site, the 6 entries of the PRD example, shared 17:42 on the day
///   EVENING     7 Nov 2026, North Site, 1 entry, not shared
///
/// and a fifth log, OPEN (8 Nov), which is the open log and so is not listed.
class HistoryWorld
{
public:
    static constexpr std::uint64_t LAST_YEAR = 1;
    static constexpr std::uint64_t YESTERDAY = 2;
    static constexpr std::uint64_t MORNING = 3;
    static constexpr std::uint64_t EVENING = 4;
    static constexpr std::uint64_t OPEN = 5;

    explicit HistoryWorld(bool share_available = true, bool with_logs = true)
        : m_Configuration(test::sample_configuration()), m_Settings(m_Gateway, m_World.environment()),
          m_Registry(m_World.environment(), m_Gateway), m_Picker(m_Settings, m_Open, m_Settings),
          m_Share(m_World.environment(), m_Open, m_Registry, m_Settings, m_Platform, share_available),
          m_History(m_World.environment(), m_Gateway, m_Registry, m_Settings, m_Open, m_Clipboard, m_Share)
    {
        m_Storage.config().set_saved(export_configuration(m_Configuration));
        m_Settings.load();
        m_Gateway.run_all();
        REQUIRE(m_Settings.configured());
        if (with_logs)
        {
            make_logs();
        }
        m_World.m_Clock.set_wall(test::utc(2026, 11, 8, 8, 10)); // 8 Nov, 19:10 in Melbourne
    }

    HistoryWorld(const HistoryWorld&) = delete;
    HistoryWorld& operator=(const HistoryWorld&) = delete;

    /// The models as the properties the screen takes.
    [[nodiscard]] QVariantMap properties()
    {
        return {{QStringLiteral("history"), QVariant::fromValue(static_cast<QObject*>(&m_History))},
                {QStringLiteral("share"), QVariant::fromValue(static_cast<QObject*>(&m_Share))},
                {QStringLiteral("picker"), QVariant::fromValue(static_cast<QObject*>(&m_Picker))},
                {QStringLiteral("settings"), QVariant::fromValue(static_cast<QObject*>(&m_Settings))}};
    }

    void settle()
    {
        m_Gateway.run_all();
    }

    /// Starts a new log, as a new day does: the open log (OPEN) is archived, so it appears in the list, and `number` is
    /// the open log from now on.
    void roll_over(std::uint64_t number)
    {
        HeaderValues header = test::sample_header();
        header.m_LocalDate = std::chrono::year{2026} / 11 / 9;
        REQUIRE(m_Storage.memory().start_new_log(test::sample_id<LogId>(OPEN), test::sample_id<LogId>(number), header));
    }

    [[nodiscard]] viewmodel::HistoryModel& history()
    {
        return m_History;
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
        return m_World.m_Clock;
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
    [[nodiscard]] bool stored(std::uint64_t number)
    {
        return m_Storage.memory().load_log(test::sample_id<LogId>(number)).has_value();
    }

private:
    void make_logs()
    {
        const LocationId north = location_named(m_Configuration, "North Site");
        make(LAST_YEAR, 2025, 11, 7, taps({{11, 9, 0}, {12, 9, 10}}), true, LocationValue{north, {}});
        make(YESTERDAY, 2026, 11, 6, taps({{21, 9, 0}}), true, LocationValue{std::nullopt, "South Elsewhere"});
        make(MORNING, 2026, 11, 7, review::prd_example(m_Configuration), true, LocationValue{north, {}});
        REQUIRE(m_Storage.memory().mark_shared(test::sample_id<LogId>(MORNING), test::utc(2026, 11, 7, 6, 42)));
        make(EVENING, 2026, 11, 7, taps({{41, 16, 0}}), true, LocationValue{north, {}});
        make(OPEN, 2026, 11, 8, taps({{51, 9, 0}}), false, LocationValue{north, {}});
        m_Events.clear();
        m_Storage.repository().clear_records();
    }

    /// One error entry of the sample configuration: its number, and the hour and minute of 7 Nov. A list of these, not
    /// of entries, because GCC reports a string of a copied initializer list as maybe uninitialised.
    struct Tap
    {
        std::uint64_t m_Number;
        int m_Hour;
        int m_Minute;
    };

    [[nodiscard]] std::vector<Entry> taps(const std::vector<Tap>& list) const
    {
        std::vector<Entry> entries;
        for (const Tap& tap : list)
        {
            entries.push_back(review::error_at(m_Configuration, tap.m_Number, tap.m_Hour, tap.m_Minute));
        }
        return entries;
    }

    void make(std::uint64_t number, int year, unsigned month, unsigned day, const std::vector<Entry>& entries,
              bool archive, std::optional<LocationValue> location = std::nullopt)
    {
        HeaderValues header = test::sample_header();
        header.m_LocalDate = std::chrono::year{year} / std::chrono::month{month} / std::chrono::day{day};
        if (location)
        {
            header.m_Location = std::move(*location);
        }
        const LogId id = test::sample_id<LogId>(number);
        REQUIRE(m_Storage.memory().create_log(id, header));
        for (const Entry& entry : entries)
        {
            REQUIRE(m_Storage.memory().add_entry(id, entry));
        }
        if (archive)
        {
            REQUIRE(m_Storage.memory().archive_log(id));
        }
        m_World.m_Clock.advance(std::chrono::minutes{1});
    }

    test::TestEnvironment m_World;
    Configuration m_Configuration;
    test::EventLog m_Events;
    test::MemoryStorage m_Storage{m_World.m_Clock, &m_Events};
    test::ManualStorageGateway m_Gateway{m_Storage.context()};
    test::FakeClipboard m_Clipboard{&m_Events};
    test::FakePlatform m_Platform{&m_Events};
    test::FakeOpenLogSource m_Open;
    viewmodel::SettingsModel m_Settings;
    viewmodel::DocumentRegistry m_Registry;
    viewmodel::ErrorPickerModel m_Picker;
    viewmodel::ShareService m_Share;
    viewmodel::HistoryModel m_History;
};

/// The new-day prompt's world: a session with the open log of Saturday 7 November 2026 (UNIT-27, North Site), a share
/// service and the watcher. The clock starts at 08:55 on the 7th in Melbourne, so nothing is up until a test moves it.
class DayWorld
{
public:
    explicit DayWorld(int entries = 7)
        : m_Env(m_World.environment()), m_Storage(m_World.m_Clock, &m_Events), m_Registry(m_Env, m_Gateway)
    {
        REQUIRE(m_Storage.memory().create_log(m_LogId, test::sample_header()));
        m_Config.set(test::sample_configuration());
        m_Session = std::make_unique<viewmodel::LogSession>(m_Env, m_Gateway, m_Registry, m_Config, m_Feedback,
                                                            viewmodel::StartupResult{});
        m_Share = std::make_unique<viewmodel::ShareService>(m_Env, *m_Session, m_Registry, m_Config, m_Platform, true);
        m_Watcher = std::make_unique<viewmodel::DayWatcher>(m_Env, *m_Session, *m_Share);
        m_Session->load();
        m_Gateway.run_all();
        for (int i = 0; i < entries; ++i)
        {
            at(melbourne(7, 9 + i, 0));
            REQUIRE(m_Session->recordError(QStringLiteral("00000000-0000-4000-8000-0000000000a1")));
            m_Gateway.run_all();
        }
        at(melbourne(7, 20, 0));
    }

    DayWorld(const DayWorld&) = delete;
    DayWorld& operator=(const DayWorld&) = delete;

    [[nodiscard]] QVariantMap properties()
    {
        return {{QStringLiteral("day"), QVariant::fromValue(static_cast<QObject*>(m_Watcher.get()))}};
    }

    void at(UtcMillis time)
    {
        m_World.m_Clock.set_wall(time);
    }

    /// It is Sunday 8 November, 09:00: the prompt is raised.
    void next_day()
    {
        at(melbourne(8, 9, 0));
        m_Watcher->check();
    }

    [[nodiscard]] viewmodel::DayWatcher& watcher()
    {
        return *m_Watcher;
    }
    [[nodiscard]] viewmodel::LogSession& session()
    {
        return *m_Session;
    }
    [[nodiscard]] viewmodel::ShareService& share()
    {
        return *m_Share;
    }
    [[nodiscard]] test::FakePlatform& platform()
    {
        return m_Platform;
    }
    [[nodiscard]] test::TestClock& clock()
    {
        return m_World.m_Clock;
    }
    [[nodiscard]] test::ManualStorageGateway& gateway()
    {
        return m_Gateway;
    }
    [[nodiscard]] test::MemoryStorage& storage()
    {
        return m_Storage;
    }

private:
    test::TestEnvironment m_World;
    viewmodel::Environment m_Env;
    test::EventLog m_Events;
    test::MemoryStorage m_Storage;
    test::ManualStorageGateway m_Gateway{m_Storage.context()};
    viewmodel::DocumentRegistry m_Registry;
    test::FakeConfigurationSource m_Config;
    test::FakeFeedbackSink m_Feedback{&m_Events};
    test::FakePlatform m_Platform{&m_Events};
    LogId m_LogId = test::sample_id<LogId>(1);
    std::unique_ptr<viewmodel::LogSession> m_Session;
    std::unique_ptr<viewmodel::ShareService> m_Share;
    std::unique_ptr<viewmodel::DayWatcher> m_Watcher;
};

} // namespace ej::ui::history
