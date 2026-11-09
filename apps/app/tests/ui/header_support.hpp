// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/configuration.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
#include "storage_samples.hpp"
#include "support/event_log.hpp"
#include "support/fake_clipboard.hpp"
#include "support/fake_sources.hpp"
#include "support/manual_storage_gateway.hpp"
#include "support/memory_storage.hpp"
#include "support/test_environment.hpp"
#include "viewmodel/document_registry.hpp"
#include "viewmodel/log_session.hpp"
#include "viewmodel/settings_model.hpp"
#include "viewmodel/timeline_model.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QQuickItem>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>
#include <algorithm>
#include <chrono>
#include <vector>

/// What the tests of the Today's details screen and the Software version sheet (package 4.9) share: the real Stage 3
/// view models over the in-memory store and the manual gateway, wired as the composition root wires them, and a few
/// lookups in the item tree.
namespace ej::ui::header
{

/// The models the two header screens use, on the fakes of Stage 3. The clock starts at Sat 7 Nov 2026 08:55 in
/// Melbourne. With `existing` the open log already has a unit, a location and a version (UNIT-27, North Site, 1.4.2);
/// without it today's log starts with three empty fields.
class HeaderWorld
{
public:
    explicit HeaderWorld(bool existing = false, const Configuration& configuration = test::sample_configuration())
        : m_Env(m_World.environment())
    {
        m_Storage.config().set_saved(export_configuration(configuration));
        m_Settings.load();
        m_Gateway.run_all();
        REQUIRE(m_Settings.configured());
        if (existing)
        {
            REQUIRE(m_Storage.memory().create_log(m_LogId, test::sample_header()));
        }
        m_Session.load();
        m_Gateway.run_all();
        REQUIRE(m_Session.state() == viewmodel::LogSession::State::OPEN);
        m_Timeline.follow(m_Session);
    }

    HeaderWorld(const HeaderWorld&) = delete;
    HeaderWorld& operator=(const HeaderWorld&) = delete;

    /// Makes the rig advance this world's clock and run its gateway.
    void attach(UiRig& rig)
    {
        rig.attach(m_World.m_Clock);
        rig.attach(m_Gateway);
    }

    [[nodiscard]] QVariantMap models() const
    {
        return {{"session", QVariant::fromValue(static_cast<QObject*>(const_cast<viewmodel::LogSession*>(&m_Session)))},
                {"settings",
                 QVariant::fromValue(static_cast<QObject*>(const_cast<viewmodel::SettingsModel*>(&m_Settings)))},
                {"timeline",
                 QVariant::fromValue(static_cast<QObject*>(const_cast<viewmodel::TimelineModel*>(&m_Timeline)))}};
    }

    [[nodiscard]] viewmodel::LogSession& session()
    {
        return m_Session;
    }
    [[nodiscard]] viewmodel::SettingsModel& settings()
    {
        return m_Settings;
    }
    [[nodiscard]] viewmodel::TimelineModel& timeline()
    {
        return m_Timeline;
    }
    [[nodiscard]] test::ManualStorageGateway& gateway()
    {
        return m_Gateway;
    }
    [[nodiscard]] test::MemoryStorage& storage()
    {
        return m_Storage;
    }
    [[nodiscard]] test::TestClock& clock()
    {
        return m_World.m_Clock;
    }
    [[nodiscard]] const LogId& log_id() const
    {
        return m_LogId;
    }

    /// Puts `count` errors in the open log's store before it is loaded is not possible; this records them through the
    /// session, as a tap does, and commits them.
    void record_errors(int count)
    {
        const QString wrong_response = QStringLiteral("00000000-0000-4000-8000-0000000000a1");
        for (int i = 0; i < count; ++i)
        {
            REQUIRE(m_Session.recordError(wrong_response));
            m_World.m_Clock.advance(std::chrono::seconds(1));
        }
        m_Gateway.run_all();
    }

private:
    test::TestEnvironment m_World;
    viewmodel::Environment m_Env;
    test::EventLog m_Events;
    test::MemoryStorage m_Storage{m_World.m_Clock, &m_Events};
    test::ManualStorageGateway m_Gateway{m_Storage.context()};
    viewmodel::DocumentRegistry m_Registry{m_Env, m_Gateway};
    viewmodel::SettingsModel m_Settings{m_Gateway, m_Env};
    test::FakeFeedbackSink m_Feedback{&m_Events};
    LogId m_LogId = test::sample_id<LogId>(1);
    viewmodel::LogSession m_Session{m_Env, m_Gateway, m_Registry, m_Settings, m_Feedback, viewmodel::StartupResult{}};
    test::FakeClipboard m_Clipboard;
    viewmodel::TimelineModel m_Timeline{m_Env, m_Settings, m_Clipboard};
};

/// The first item under `from` (the root by default) with this objectName; the test stops here when there is none, so a
/// pointer from `need` can be used without a check.
[[nodiscard]] inline QQuickItem* need(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    QQuickItem* item = rig.find(name, from);
    REQUIRE(item != nullptr);
    return item;
}

/// The first item under the rig's root with this accessible name (or text, for a `Text`), or null.
[[nodiscard]] inline QQuickItem* find_named(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    for (const Node& node : rig.tree(from))
    {
        if (node.m_Visible && node.m_Name == name)
        {
            return node.m_Item;
        }
    }
    return nullptr;
}

/// The text field inside a `FormField`.
[[nodiscard]] inline QQuickItem* input_of(UiRig& rig, QQuickItem* form_field)
{
    REQUIRE(form_field != nullptr);
    QQuickItem* input = rig.find(QStringLiteral("formInput"), form_field);
    REQUIRE(input != nullptr);
    return input;
}

/// Taps a field, types `text` after clearing it and presses Return, as a person would.
inline void retype(UiRig& rig, QQuickItem* form_field, const QString& text)
{
    QQuickItem* input = input_of(rig, form_field);
    REQUIRE(input != nullptr);
    rig.tap(input);
    // The caret goes to the end first: a value set at the right of its row (the version sheet's) is tapped to the left
    // of its text, which puts the caret before it.
    rig.key(Qt::Key_End);
    const qsizetype existing = input->property("text").toString().size();
    for (qsizetype i = 0; i < existing; ++i)
    {
        rig.key(Qt::Key_Backspace);
    }
    rig.type(text);
    rig.key(Qt::Key_Return);
    rig.settle();
    rig.layout();
}

/// Plays `ms` of animation one frame at a time.
inline void play(UiRig& rig, int ms)
{
    for (int done = 0; done < ms; done += 16)
    {
        rig.advance(std::chrono::milliseconds(16));
    }
}

/// The Environment sheet of Today's details (PRF-11), which the tests of the screen and of the sheet share: the names
/// of the choices on the sheet, in the order they are drawn, with None as "".
inline QStringList sheet_choices(UiRig& rig)
{
    struct Choice
    {
        qreal m_Top;
        QString m_Name;
    };
    std::vector<Choice> found;
    for (const Node& node : rig.tree())
    {
        if (node.m_ObjectName == QStringLiteral("environmentNone"))
        {
            found.push_back({node.m_Rect.top(), QString()});
        }
        else if (node.m_ObjectName.startsWith(QStringLiteral("environment_")))
        {
            found.push_back({node.m_Rect.top(), node.m_ObjectName.mid(QStringLiteral("environment_").size())});
        }
    }
    std::ranges::sort(found, [](const Choice& a, const Choice& b) { return a.m_Top < b.m_Top; });
    QStringList names;
    for (const Choice& choice : found)
    {
        names.push_back(choice.m_Name);
    }
    return names;
}

inline QQuickItem* choice(UiRig& rig, const char* name)
{
    // The choices are made by a Repeater, which `find` does not reach: they are found in the item tree.
    for (const Node& node : rig.tree())
    {
        if (node.m_ObjectName == QString::fromLatin1(name))
        {
            return node.m_Item;
        }
    }
    FAIL("no choice named " << name);
}

inline void open_environment_sheet(UiRig& rig)
{
    rig.tap(need(rig, QStringLiteral("headerEnvironment")));
    play(rig, 500);
    REQUIRE(need(rig, QStringLiteral("environmentSheet"))->property("isOpen").toBool());
}

} // namespace ej::ui::header
