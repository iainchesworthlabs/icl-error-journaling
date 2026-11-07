// SPDX-License-Identifier: GPL-3.0-or-later
// NewDay (docs/plan/stage-4.md, 4.11): newday.visibility, newday.text, newday.unshared, newday.actions,
// newday.share_first_flow and the cross-cutting checks.
#include "history_test_support.hpp"
// The other headers of the tests come after the project's, as the support header says.
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QByteArray>
#include <QMetaObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QString>
#include <chrono>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::history;
using ej::bridge::ShareOutcome;
using ej::test::RepositoryOp;
using namespace std::chrono_literals;

namespace
{

// The prompt in the layer that the shell keeps over everything, above a control that a tap could reach.
constexpr auto HOST = R"(
    import QtQuick
    import ErrorJournalingApp
    SheetHost {
        id: host
        required property var day
        Rectangle {
            objectName: "below"
            property int taps: 0
            x: 20; y: 600; width: 120; height: 54
            color: Theme.colour.primary
            Accessible.role: Accessible.Button
            Accessible.name: "Below the prompt"
            TapHandler { onTapped: parent.taps += 1 }
        }
        sheets: [
            NewDay {
                objectName: "newDay"
                day: host.day
                overrideInsets: true
                overrideTop: 59
                overrideBottom: 34
            }
        ]
    })";

/// The real DayWatcher over a log with entries, and the prompt over it. The rig is declared after the world, so it goes
/// first when this is destroyed and no item outlives a model it reads.
struct Screen
{
    explicit Screen(int entries = 7) : m_World(entries)
    {
        m_Rig.attach(m_World.clock());
        m_Rig.attach(m_World.gateway());
        m_Host = m_Rig.load_source(QByteArray(HOST), m_World.properties());
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_Host != nullptr);
        m_Prompt = m_Rig.find(QStringLiteral("newDay"));
        REQUIRE(m_Prompt != nullptr);
    }

    DayWorld m_World;
    UiRig m_Rig;
    QQuickItem* m_Host = nullptr;
    QQuickItem* m_Prompt = nullptr;

    /// It is Sunday: the model raises the prompt and the alert presents.
    void raise()
    {
        m_World.next_day();
        m_Rig.advance(500ms);
    }

    /// The id of the open log; the test stops when there is none.
    [[nodiscard]] LogId open_id()
    {
        REQUIRE(m_World.session().open_log() != nullptr);
        return m_World.session().open_log()->id();
    }
    [[nodiscard]] bool is_open() const
    {
        return m_Prompt->property("isOpen").toBool();
    }
    [[nodiscard]] QQuickItem* named(const char* name) const
    {
        return m_Rig.find(QString::fromLatin1(name));
    }
    [[nodiscard]] bool has_button(const char* name) const
    {
        QQuickItem* item = named(name);
        return item != nullptr && item->isVisible();
    }
    [[nodiscard]] QString text(const char* name) const
    {
        QQuickItem* item = named(name);
        return item != nullptr ? item->property("text").toString() : QString();
    }
    [[nodiscard]] QString label_of(const char* name) const
    {
        QQuickItem* item = named(name);
        return item != nullptr ? item->property("label").toString() : QString();
    }
    void tap(const char* name)
    {
        QQuickItem* item = named(name);
        REQUIRE(item != nullptr);
        m_Rig.tap(item);
    }
    /// What the prompt owns after a tap: the commit, then the end of the alert's own animation.
    void settle_and_close()
    {
        m_Rig.settle();
        m_Rig.advance(600ms);
    }
};

} // namespace

TEST_CASE(
    "newday.visibility: the alert follows promptVisible, blocks what is under it and is not dismissed from outside",
    "[ui][newday]")
{
    Screen s;
    QQuickItem* below = s.m_Rig.find(QStringLiteral("below"));
    REQUIRE(below != nullptr);

    SECTION("nothing is up before the new day, and the control under it works")
    {
        CHECK_FALSE(s.m_World.watcher().prompt_visible());
        CHECK_FALSE(s.is_open());
        s.m_Rig.tap(below);
        CHECK(below->property("taps").toInt() == 1);
    }

    SECTION("the alert comes up with the prompt and blocks the tab bar")
    {
        s.raise();
        REQUIRE(s.m_World.watcher().prompt_visible());
        CHECK(s.is_open());
        s.m_Rig.tap(below);
        CHECK(below->property("taps").toInt() == 0);
    }

    SECTION("a tap on the scrim, a drag of the card and Escape leave it up")
    {
        s.raise();
        REQUIRE(s.is_open());
        s.m_Rig.tap(s.named("sheetScrim"), QPointF(10, 10));
        s.m_Rig.advance(500ms);
        CHECK(s.is_open());
        const QRectF card = s.m_Rig.geometry(s.named("sheetPanel"));
        s.m_Rig.drag(s.m_Host, card.center(), card.center() + QPointF(0, 300));
        s.m_Rig.advance(500ms);
        CHECK(s.is_open());
        s.m_Rig.key(Qt::Key_Escape);
        s.m_Rig.advance(500ms);
        CHECK(s.is_open());
        CHECK(s.m_World.watcher().prompt_visible());
    }

    SECTION("the alert goes when the model takes the prompt down, and comes back with the next check")
    {
        s.raise();
        REQUIRE(s.is_open());
        s.m_World.watcher().keepOpen();
        s.m_Rig.advance(600ms);
        CHECK_FALSE(s.is_open());
        s.m_World.watcher().check();
        s.m_Rig.advance(600ms);
        CHECK(s.is_open());
    }
}

TEST_CASE("newday.text: the new date and the old day come from the model and are shown without edits", "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    CHECK(s.m_World.watcher().new_date_text() == QStringLiteral("Sunday 8 November"));

    CHECK(s.text("alertTitle") == QStringLiteral("It’s Sunday 8 November"));
    CHECK(s.text("alertMessage") ==
          QStringLiteral(
              "Saturday’s log has 7 entries and hasn’t been shared yet. Starting a new log keeps it in History."));
    CHECK(s.label_of("alertConfirm") == QStringLiteral("Start a new log"));
    CHECK(s.label_of("alertAlternate") == QStringLiteral("Share Saturday’s log first"));
    CHECK(s.label_of("alertCancel") == QStringLiteral("Keep Saturday’s log open"));

    // The panel is a dialog named by the title and described by the message.
    for (const Node& node : s.m_Rig.tree())
    {
        if (node.m_ObjectName == QStringLiteral("sheetPanel"))
        {
            CHECK(node.m_Name == QStringLiteral("It’s Sunday 8 November"));
            CHECK(node.m_Description == s.text("alertMessage"));
        }
    }
}

TEST_CASE("newday.text: one entry is one entry, and a day that was already shared says nothing of it", "[ui][newday]")
{
    SECTION("one entry")
    {
        Screen s(1);
        s.raise();
        CHECK(s.text("alertMessage") ==
              QStringLiteral(
                  "Saturday’s log has 1 entry and hasn’t been shared yet. Starting a new log keeps it in History."));
    }

    SECTION("a log with no entry is never called unshared")
    {
        Screen s(0);
        s.raise();
        REQUIRE(s.is_open());
        CHECK_FALSE(s.m_World.watcher().unshared());
        CHECK(s.text("alertMessage") ==
              QStringLiteral("Saturday’s log has 0 entries. Starting a new log keeps it in History."));
        CHECK_FALSE(s.has_button("alertAlternate"));
    }
}

TEST_CASE("newday.unshared: the warning and the share-first choice go the moment a share is recorded", "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    REQUIRE(s.m_World.watcher().unshared());
    CHECK(s.has_button("alertAlternate"));
    CHECK(s.text("alertMessage").contains(QStringLiteral("hasn’t been shared yet")));

    // The log is shared from elsewhere while the prompt is up.
    REQUIRE(s.m_World.share().shareOpenLog());
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);

    REQUIRE(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.m_World.watcher().unshared());
    CHECK(s.is_open());
    CHECK_FALSE(s.has_button("alertAlternate"));
    CHECK(s.text("alertMessage") ==
          QStringLiteral("Saturday’s log has 7 entries. Starting a new log keeps it in History."));
    CHECK(s.has_button("alertConfirm"));
    CHECK(s.has_button("alertCancel"));
}

TEST_CASE("newday.actions: Start a new log calls acceptNewLog once and the prompt goes after the commit",
          "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    const LogId before = s.open_id();

    s.tap("alertConfirm");
    s.settle_and_close();

    CHECK_FALSE(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.is_open());
    REQUIRE(s.m_World.session().open_log() != nullptr);
    CHECK_FALSE(s.open_id() == before);
    // One log was archived and one was made.
    const auto logs = s.m_World.storage().memory().list_logs();
    REQUIRE(logs.has_value());
    CHECK(logs->size() == 2);
    // The new day is not a tap on a tile: nothing fires a haptic.
    CHECK(s.m_World.platform().haptics().empty());
}

TEST_CASE("newday.actions: Start a new log that is refused keeps the prompt and says so", "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    // A new log is on its way already, so the session refuses a second.
    REQUIRE(s.m_World.session().startNewLog());

    s.tap("alertConfirm");
    s.m_Rig.advance(600ms);

    CHECK(s.m_World.watcher().prompt_visible());
    CHECK(s.is_open());
    CHECK(s.text("alertMessage") ==
          QStringLiteral("A new log could not be started. Nothing was changed, and Saturday’s log is still open."));
    CHECK(s.has_button("alertConfirm"));

    // The refused attempt did nothing else: once the first new log is committed the prompt goes.
    s.m_Rig.settle();
    s.m_Rig.advance(600ms);
    CHECK_FALSE(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.is_open());
}

TEST_CASE("newday.actions: a new log whose write fails leaves the prompt up and the alert comes back", "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    s.m_World.storage().repository().fail_next(RepositoryOp::START_NEW_LOG);

    s.tap("alertConfirm");
    s.settle_and_close();

    CHECK(s.m_World.watcher().prompt_visible());
    CHECK(s.is_open());

    // Trying again works.
    s.tap("alertConfirm");
    s.settle_and_close();
    CHECK_FALSE(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.is_open());
}

TEST_CASE("newday.actions: Keep the log open hides the prompt and changes nothing", "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    const LogId before = s.open_id();

    s.tap("alertCancel");
    s.settle_and_close();

    CHECK_FALSE(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.is_open());
    CHECK(s.open_id() == before);
    CHECK(s.m_World.platform().sheets_presented() == 0);
    CHECK(s.m_World.platform().haptics().empty());

    // The words keep their last values while the card is gone, so a fade has something to show.
    CHECK(s.m_World.watcher().new_date_text() == QStringLiteral("Sunday 8 November"));
    CHECK(s.text("alertTitle") == QStringLiteral("It’s Sunday 8 November"));
}

TEST_CASE("newday.share_first_flow: Share first leaves the prompt up, then Start works", "[ui][newday]")
{
    Screen s;
    s.raise();
    REQUIRE(s.is_open());
    s.m_World.platform().hold_completion();

    s.tap("alertAlternate");
    s.m_Rig.settle();
    // The share sheet is up; the prompt is still the model's.
    CHECK(s.m_World.platform().sheets_presented() == 1);
    CHECK(s.m_World.watcher().prompt_visible());
    s.m_Rig.advance(600ms);
    CHECK(s.is_open());

    REQUIRE(s.m_World.platform().complete_share_sheet(ShareOutcome::SHARED));
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);

    CHECK(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.m_World.watcher().unshared());
    CHECK(s.is_open());
    CHECK_FALSE(s.has_button("alertAlternate"));

    s.tap("alertConfirm");
    s.settle_and_close();
    CHECK_FALSE(s.m_World.watcher().prompt_visible());
    CHECK_FALSE(s.is_open());
    CHECK(s.m_World.platform().haptics().empty());
}

TEST_CASE("newday.share_first_flow: a share that is cancelled leaves the prompt as it was", "[ui][newday]")
{
    Screen s;
    s.raise();
    s.m_World.platform().answer_with(ShareOutcome::CANCELLED);

    s.tap("alertAlternate");
    s.settle_and_close();

    CHECK(s.m_World.watcher().prompt_visible());
    CHECK(s.m_World.watcher().unshared());
    CHECK(s.is_open());
    CHECK(s.has_button("alertAlternate"));
}

TEST_CASE("newday.cross_cutting: names, targets, order and text scale, in light and dark", "[ui][newday]")
{
    const bool dark = GENERATE(false, true);
    const int entries = GENERATE(7, 0);
    INFO("dark " << dark << ", entries " << entries);
    Screen s(entries);
    s.m_Rig.set_dark(dark);
    // The control under the prompt is disabled but still in the tree; the reading order of the alert is what is
    // checked.
    QQuickItem* below = s.m_Rig.find(QStringLiteral("below"));
    REQUIRE(below != nullptr);
    below->setVisible(false);
    s.raise();
    REQUIRE(s.is_open());
    expect_clean(check_everything(s.m_Rig));
    expect_clean(check_safe_area(s.m_Rig, 59, 34));
}

TEST_CASE("newday.cross_cutting: with Reduce Motion the alert fades in 100 ms and nothing slides",
          "[ui][newday][motion]")
{
    Screen s;
    s.m_Rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(s.m_Rig));
    s.m_World.next_day();
    s.m_Rig.advance(250ms);
    CHECK(s.is_open());
    QQuickItem* panel = s.named("sheetPanel");
    REQUIRE(panel != nullptr);
    CHECK(panel->opacity() == 1);
    CHECK(panel->scale() == 1);
    s.tap("alertCancel");
    s.m_Rig.advance(250ms);
    CHECK_FALSE(s.is_open());
}
