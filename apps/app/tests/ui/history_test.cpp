// SPDX-License-Identifier: GPL-3.0-or-later
// History (docs/plan/stage-4.md, 4.11): history.sections, history.refresh, history.open, history.reshare,
// history.delete, history.empty, history.leave and the cross-cutting checks. The New day prompt has its own file,
// new_day_test.cpp.
#include "history_test_support.hpp"
// The other headers of the tests come after the project's, as the support header says.
#include "golden.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAccessible>
#include <QMetaObject>
#include <QObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <chrono>
#include <utility>
#include <vector>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::history;
using ej::bridge::ShareOutcome;
using ej::test::RepositoryOp;
using namespace std::chrono_literals;

namespace
{

/// The real view models over the in-memory store, and the History screen over them. The rig is declared after the
/// world, so it goes first when this is destroyed and no item outlives a model it reads.
struct Screen
{
    explicit Screen(bool share_available = true, bool with_logs = true, bool show = true)
        : m_World(share_available, with_logs)
    {
        m_Rig.attach(m_World.clock());
        m_Rig.attach(m_World.gateway());
        m_History = m_Rig.load(QStringLiteral("History"), m_World.properties());
        INFO(m_Rig.errors().toStdString());
        REQUIRE(m_History != nullptr);
        if (show)
        {
            shown();
        }
    }

    HistoryWorld m_World;
    UiRig m_Rig;
    QQuickItem* m_History = nullptr;

    /// The shell shows the tab, and the list arrives.
    void shown()
    {
        QMetaObject::invokeMethod(m_History, "shown");
        m_Rig.settle();
        m_Rig.advance(100ms);
    }

    void hidden()
    {
        QMetaObject::invokeMethod(m_History, "hidden");
        m_Rig.settle();
        m_Rig.advance(600ms);
    }

    [[nodiscard]] QQuickItem* named(const char* name, QQuickItem* from = nullptr)
    {
        return find_in(m_Rig, QString::fromLatin1(name), from);
    }

    /// An item that must be there: the test stops with a failure when there is none.
    [[nodiscard]] QQuickItem* required(const char* name, QQuickItem* from = nullptr)
    {
        return review::require_in(m_Rig, QString::fromLatin1(name), from);
    }

    [[nodiscard]] bool visible(const char* name, QQuickItem* from = nullptr)
    {
        QQuickItem* item = named(name, from);
        return item != nullptr && item->isVisible() && m_Rig.geometry(item).height() > 0;
    }

    [[nodiscard]] QString text(const char* name, QQuickItem* from = nullptr)
    {
        return text_in(m_Rig, QString::fromLatin1(name), from);
    }

    /// The delegates of the list in use, top to bottom.
    [[nodiscard]] std::vector<QQuickItem*> rows()
    {
        std::vector<QQuickItem*> out;
        QQuickItem* list = named("historyList");
        REQUIRE(list != nullptr);
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

    [[nodiscard]] QQuickItem* row(int index)
    {
        const std::vector<QQuickItem*> all = rows();
        REQUIRE(static_cast<int>(all.size()) > index);
        return all[static_cast<std::size_t>(index)];
    }

    /// The place, the detail and the status of a row, as the screen shows them.
    [[nodiscard]] QString place(int index)
    {
        return text("historyPlace", row(index));
    }
    [[nodiscard]] QString detail(int index)
    {
        return text("historyDetail", row(index));
    }
    [[nodiscard]] QString status(int index)
    {
        return text("historyStatus", row(index));
    }

    [[nodiscard]] int list_count()
    {
        return required("historyList")->property("count").toInt();
    }

    /// The index of the row of log `number`, by the order the model gives.
    [[nodiscard]] int index_of(std::uint64_t number)
    {
        auto& history = m_World.history();
        for (int i = 0; i < history.rowCount(); ++i)
        {
            if (history.data(history.index(i), viewmodel::HistoryModel::LOG_ID).toString() == log_text(number))
            {
                return i;
            }
        }
        return -1;
    }

    void tap_row(int index)
    {
        m_Rig.tap(named("face", row(index)));
        m_Rig.settle();
        m_Rig.advance(500ms);
    }

    /// A swipe to the left on a row, which opens its actions.
    void swipe_open(int index)
    {
        QQuickItem* face = named("face", row(index));
        REQUIRE(face != nullptr);
        m_Rig.swipe(face, QPointF(face->width() - 60, face->height() / 2), QPointF(60, face->height() / 2));
        m_Rig.advance(400ms);
    }

    void tap_action(int index, const char* action)
    {
        swipe_open(index);
        QQuickItem* button = named(action, row(index));
        REQUIRE(button != nullptr);
        m_Rig.tap(button);
    }

    /// Opens the log of `number` and waits for the page to arrive.
    void open(std::uint64_t number)
    {
        const int index = index_of(number);
        REQUIRE(index >= 0);
        tap_row(index);
        REQUIRE(named("reviewList") != nullptr);
    }

    [[nodiscard]] bool review_shown()
    {
        return m_History->property("reviewShown").toBool();
    }

    [[nodiscard]] bool alert_open()
    {
        QQuickItem* alert = named("deleteAlert");
        return alert != nullptr && alert->property("isOpen").toBool();
    }

    [[nodiscard]] QString notice_title()
    {
        return visible("historyNotice") ? text("bannerTitle", named("historyNotice")) : QString();
    }
};

} // namespace

TEST_CASE("history.sections: newest first, one heading for two logs of a date, the shared state and its time",
          "[ui][history]")
{
    Screen s;
    REQUIRE(s.list_count() == 4);

    // Newest first: the two logs of 7 Nov (the later one first), then 6 Nov, then a year before.
    CHECK(s.index_of(HistoryWorld::EVENING) == 0);
    CHECK(s.index_of(HistoryWorld::MORNING) == 1);
    CHECK(s.index_of(HistoryWorld::YESTERDAY) == 2);
    CHECK(s.index_of(HistoryWorld::LAST_YEAR) == 3);

    // Three headings: the two logs of 7 Nov 2026 are under one.
    std::vector<QString> headings;
    for (QQuickItem* heading : all_named(s.m_Rig, QStringLiteral("historySection")))
    {
        if (heading->isVisible())
        {
            headings.push_back(heading->property("text").toString());
        }
    }
    REQUIRE(headings.size() == 3);
    CHECK(headings[0] == QStringLiteral("7 NOV 2026"));
    CHECK(headings[1] == QStringLiteral("6 NOV 2026"));
    CHECK(headings[2] == QStringLiteral("7 NOV 2025"));

    // The rows: the unit and the site, the date and the entries, and whether and when the log was shared.
    CHECK(s.place(0) == QStringLiteral("UNIT-27 · North Site"));
    CHECK(s.detail(0) == QStringLiteral("Sat 7 Nov · 1 entry"));
    CHECK(s.status(0) == QStringLiteral("Not shared"));
    CHECK(s.place(1) == QStringLiteral("UNIT-27 · North Site"));
    CHECK(s.detail(1) == QStringLiteral("Sat 7 Nov · 6 entries"));
    CHECK(s.status(1) == QStringLiteral("Shared 17:42"));
    CHECK(s.place(2) == QStringLiteral("UNIT-27 · South Elsewhere"));
    CHECK(s.detail(2) == QStringLiteral("Fri 6 Nov · 1 entry"));
    CHECK(s.detail(3) == QStringLiteral("Fri 7 Nov · 2 entries"));

    // The two rows of one date touch; the rows of different dates have a heading between them.
    const QRectF first = s.m_Rig.geometry(s.row(0));
    const QRectF second = s.m_Rig.geometry(s.row(1));
    const QRectF third = s.m_Rig.geometry(s.row(2));
    CHECK(second.top() == first.bottom());
    CHECK(third.top() > second.bottom());

    // The subtitle of the design, and no empty state.
    CHECK(s.text("historySubtitle") == QStringLiteral("Past logs, newest first. Today’s log is on the Review tab."));
    CHECK_FALSE(s.visible("historyPlaceholder"));

    // The rows are one button each, named with the date, the place, the entries and the state.
    CHECK(s.required("historySwipe", s.row(1))->property("accessibleName").toString() ==
          QStringLiteral("Sat 7 Nov, UNIT-27 · North Site, 6 entries, Shared 17:42"));
}

TEST_CASE("history.sections: the shared time is in the user's time format", "[ui][history]")
{
    Screen s;
    REQUIRE(s.m_World.settings().setTimeFormat("12h", true, "lower").isEmpty());
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);
    const int morning = s.index_of(HistoryWorld::MORNING);
    const QString shared = s.m_World.history()
                               .data(s.m_World.history().index(morning), viewmodel::HistoryModel::SHARED_AT_TEXT)
                               .toString();
    CHECK(shared == QStringLiteral("05:42:00 pm"));
    CHECK(s.status(morning) == QStringLiteral("Shared 05:42:00 pm"));
}

TEST_CASE("history.sections: the open log is not in the list", "[ui][history]")
{
    Screen s;
    for (int i = 0; i < s.list_count(); ++i)
    {
        CHECK(s.index_of(HistoryWorld::OPEN) == -1);
        CHECK(s.m_World.history().data(s.m_World.history().index(i), viewmodel::HistoryModel::LOG_ID).toString() !=
              log_text(HistoryWorld::OPEN));
    }
    // So no row offers to delete it, and the model would not give a token for it.
    CHECK(s.m_World.history().requestDelete(log_text(HistoryWorld::OPEN)).isEmpty());
}

TEST_CASE("history.refresh: shown reads the list, with a loading state and one reset", "[ui][history]")
{
    Screen s(true, true, false);
    QSignalSpy resets(&s.m_World.history(), &QAbstractItemModel::modelReset);

    // Before the first read there is nothing to say about the logs.
    CHECK_FALSE(s.visible("historyPlaceholder"));

    QMetaObject::invokeMethod(s.m_History, "shown");
    s.m_Rig.advance(50ms);
    CHECK(s.m_World.history().loading());
    CHECK(s.list_count() == 0);
    REQUIRE(s.visible("historyPlaceholder"));
    CHECK(s.text("historyEmptyTitle") == QStringLiteral("Loading the logs…"));
    CHECK_FALSE(s.visible("historyEmptyText"));

    s.m_Rig.settle();
    s.m_Rig.advance(100ms);
    CHECK_FALSE(s.m_World.history().loading());
    CHECK(s.list_count() == 4);
    CHECK_FALSE(s.visible("historyPlaceholder"));
    CHECK(resets.count() == 1);

    // Shown again with logs on screen: they stay while the list is read, and arrive in one reset.
    QMetaObject::invokeMethod(s.m_History, "shown");
    s.m_Rig.advance(50ms);
    CHECK(s.m_World.history().loading());
    CHECK(s.list_count() == 4);
    CHECK_FALSE(s.visible("historyPlaceholder"));
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);
    CHECK(s.list_count() == 4);
    CHECK(resets.count() == 2);
}

TEST_CASE("history.refresh: a log that is new since the last read appears when the tab is shown again", "[ui][history]")
{
    Screen s;
    REQUIRE(s.list_count() == 4);
    s.m_World.roll_over(6);
    s.shown();
    REQUIRE(s.list_count() == 5);
    // The log of 8 Nov is the newest, and has a heading of its own.
    CHECK(s.index_of(HistoryWorld::OPEN) == 0);
}

TEST_CASE("history.refresh: an error keeps the old list and shows a banner", "[ui][history]")
{
    Screen s;
    REQUIRE(s.list_count() == 4);
    s.m_World.storage().repository().fail_next(RepositoryOp::LIST_LOGS);

    s.shown();

    CHECK(s.list_count() == 4);
    REQUIRE(s.visible("historyNotice"));
    CHECK(s.notice_title() == QStringLiteral("History could not be updated"));
    CHECK(s.text("bannerMessage", s.named("historyNotice")) == QStringLiteral("The list of logs could not be read."));

    // The banner is closed by its button, and the next read clears it as well.
    s.m_Rig.tap(s.named("bannerClose", s.named("historyNotice")));
    CHECK_FALSE(s.visible("historyNotice"));
    s.shown();
    CHECK(s.list_count() == 4);
    CHECK_FALSE(s.visible("historyNotice"));
}

TEST_CASE("history.refresh: an error on the first read says so and does not claim there are no logs", "[ui][history]")
{
    Screen s(true, true, false);
    s.m_World.storage().repository().fail_next(RepositoryOp::LIST_LOGS);

    s.shown();

    CHECK(s.list_count() == 0);
    CHECK(s.visible("historyNotice"));
    CHECK_FALSE(s.visible("historyPlaceholder"));
}

TEST_CASE("history.empty: no archived logs shows an empty state", "[ui][history]")
{
    Screen s(true, false);

    REQUIRE(s.visible("historyPlaceholder"));
    CHECK(s.text("historyEmptyTitle") == QStringLiteral("No past logs yet"));
    CHECK(
        s.text("historyEmptyText") ==
        QStringLiteral(
            "When you start a new day, yesterday’s log moves here, where you can open it, copy it or share it again."));
    CHECK(s.visible("historyEmptyIcon"));
    // The title stays; the line about newest first goes, as in the design.
    CHECK(s.visible("historyTitle"));
    CHECK_FALSE(s.visible("historySubtitle"));
    CHECK_FALSE(s.visible("historyNotice"));

    expect_clean(check_everything(s.m_Rig));
    s.m_Rig.set_dark(true);
    expect_clean(check_everything(s.m_Rig));
}

TEST_CASE("history.open: a row opens a review of the past log, its lines are there, and back closes the log",
          "[ui][history]")
{
    Screen s;
    REQUIRE(s.m_World.history().review_model() == nullptr);

    s.open(HistoryWorld::MORNING);

    REQUIRE(s.m_World.history().review_model() != nullptr);
    CHECK(s.review_shown());
    // The page is visible and fills the stack: not the blank page of a transition that left it faded out.
    QQuickItem* page = s.named("reviewList");
    REQUIRE(page != nullptr);
    CHECK(s.visible("reviewList"));
    CHECK(s.m_Rig.geometry(page).width() > 0);
    QQuickItem* back = s.named("reviewBack");
    REQUIRE(back != nullptr);
    CHECK(s.visible("reviewBack"));
    CHECK(s.m_Rig.geometry(back).left() >= 0);

    // The lines of the log, in the words of the export, with the date and when it was shared.
    CHECK(review::joined(review::row_lines(s.m_Rig)) == review::joined(review::prd_example_lines()));
    CHECK(s.text("reviewTitle") == QStringLiteral("Sat 7 Nov"));
    CHECK(s.text("reviewShared") == QStringLiteral("Shared 7 Nov 2026 at 17:42"));
    CHECK(s.required("reviewShare")->property("text").toString() == QStringLiteral("Share log again"));
    CHECK(s.m_World.platform().haptics().empty());

    // Back slides the page away and then lets the log go.
    s.m_Rig.tap(back);
    s.m_Rig.advance(50ms);
    CHECK(s.m_World.history().review_model() != nullptr); // still showing while it slides
    s.m_Rig.advance(600ms);
    s.m_Rig.settle();
    CHECK_FALSE(s.review_shown());
    CHECK(s.m_World.history().review_model() == nullptr);
    CHECK(s.visible("historyList"));
    CHECK(s.list_count() == 4);
}

TEST_CASE("history.open: copying and editing work on the past log", "[ui][history]")
{
    Screen s;
    s.open(HistoryWorld::MORNING);

    // Copy-all puts the past log, not the open one, on the clipboard.
    s.m_Rig.tap(s.named("reviewCopyAll"));
    test::check_golden("prd-example", s.m_World.clipboard().text());

    // A tap on a line opens the Edit entry sheet.
    const std::vector<QQuickItem*> lines = review::delegates(s.m_Rig);
    REQUIRE_FALSE(lines.empty());
    s.m_Rig.tap(s.named("face", lines.front()));
    s.m_Rig.advance(500ms);
    QObject* editor = s.m_History->findChild<QObject*>(QStringLiteral("editSheet"));
    REQUIRE(editor != nullptr);
    CHECK(editor->property("isOpen").toBool());
}

TEST_CASE("history.open: an entry deleted in a past log changes the count of its row", "[ui][history]")
{
    Screen s;
    s.open(HistoryWorld::MORNING);

    const std::vector<QQuickItem*> lines = review::delegates(s.m_Rig);
    REQUIRE_FALSE(lines.empty());
    QQuickItem* face = s.named("face", lines.front());
    s.m_Rig.swipe(face, QPointF(face->width() - 60, face->height() / 2), QPointF(60, face->height() / 2));
    s.m_Rig.advance(400ms);
    s.m_Rig.tap(s.named("deleteAction", lines.front()));
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);
    CHECK(review::row_lines(s.m_Rig).size() == 5);

    s.m_Rig.tap(s.named("reviewBack"));
    s.m_Rig.advance(700ms);
    s.m_Rig.settle();
    CHECK(s.detail(s.index_of(HistoryWorld::MORNING)) == QStringLiteral("Sat 7 Nov · 5 entries"));
}

TEST_CASE("history.open: a tap over the buttons under a closed row opens the log and does nothing else",
          "[ui][history]")
{
    Screen s;
    const int morning = s.index_of(HistoryWorld::MORNING);
    QQuickItem* face = s.named("face", s.row(morning));
    REQUIRE(face != nullptr);

    // Where Share and Delete are, under the row: the tap is a tap on the row.
    const QRectF share = s.m_Rig.geometry(s.named("copyAction", s.row(morning)));
    const QRectF row = s.m_Rig.geometry(face);
    REQUIRE(share.left() < row.right());
    s.m_Rig.tap(face, QPointF(share.left() - row.left() + 8, row.height() / 2));
    s.m_Rig.settle();
    s.m_Rig.advance(500ms);

    CHECK(s.review_shown());
    CHECK(s.m_World.platform().sheets_presented() == 0);
    CHECK_FALSE(s.alert_open());
    CHECK_FALSE(s.visible("historyNotice"));
}

TEST_CASE("history.open: the buttons under a row can be pressed by a screen reader, and the row too", "[ui][history]")
{
    Screen s;
    const int evening = s.index_of(HistoryWorld::EVENING);
    QQuickItem* row = s.row(evening);

    QQuickItem* share = s.required("copyAction", row);
    QQuickItem* del = s.required("deleteAction", row);
    CHECK(input::require_interface(share)->text(QAccessible::Name) ==
          QStringLiteral("Share, Sat 7 Nov, UNIT-27 · North Site, 1 entry, Not shared"));
    CHECK(input::require_interface(del)->text(QAccessible::Name) ==
          QStringLiteral("Delete, Sat 7 Nov, UNIT-27 · North Site, 1 entry, Not shared"));

    input::press_action(share);
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);
    CHECK(s.m_World.platform().sheets_presented() == 1);
    CHECK(s.status(evening) == QStringLiteral("Shared 19:10"));

    input::press_action(del);
    s.m_Rig.advance(500ms);
    CHECK(s.alert_open());
    s.m_Rig.tap(s.named("alertCancel"));
    s.m_Rig.advance(600ms);

    input::press_action(s.required("face", row));
    s.m_Rig.settle();
    s.m_Rig.advance(500ms);
    CHECK(s.review_shown());
}

TEST_CASE("history.open: a log that cannot be read says so and opens nothing", "[ui][history]")
{
    Screen s;
    s.m_World.storage().repository().fail_next(RepositoryOp::LOAD_LOG);

    s.tap_row(s.index_of(HistoryWorld::MORNING));

    CHECK_FALSE(s.review_shown());
    CHECK(s.m_World.history().review_model() == nullptr);
    CHECK(s.notice_title() == QStringLiteral("The log could not be opened"));

    // Asking again works: the screen is not stuck waiting for the first answer.
    s.m_Rig.tap(s.named("bannerClose", s.named("historyNotice")));
    s.tap_row(s.index_of(HistoryWorld::MORNING));
    CHECK(s.review_shown());
}

TEST_CASE("history.leave: leaving the tab goes back to the list and lets the log go", "[ui][history]")
{
    Screen s;
    s.open(HistoryWorld::MORNING);
    REQUIRE(s.review_shown());

    s.hidden();

    CHECK_FALSE(s.review_shown());
    CHECK(s.m_World.history().review_model() == nullptr);

    // Shown again, it is the list again.
    s.shown();
    CHECK(s.list_count() == 4);
    CHECK(s.visible("historyList"));
}

TEST_CASE("history.reshare: a swipe on a row shares that log and its state updates", "[ui][history]")
{
    Screen s;
    const int evening = s.index_of(HistoryWorld::EVENING);
    REQUIRE(s.status(evening) == QStringLiteral("Not shared"));

    s.tap_action(evening, "copyAction");
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);

    REQUIRE(s.m_World.platform().sheets_presented() == 1);
    CHECK(s.m_World.platform().shared_texts().front().find("Software Version") != std::string::npos);
    CHECK(s.status(evening) == QStringLiteral("Shared 19:10"));
    CHECK_FALSE(s.visible("historyNotice"));
    // The order of the rows did not change.
    CHECK(s.index_of(HistoryWorld::EVENING) == evening);
    CHECK(s.m_World.platform().haptics().empty());
}

TEST_CASE("history.reshare: a share that fails says so and changes nothing", "[ui][history]")
{
    Screen s;
    s.m_World.platform().answer_with(ShareOutcome::FAILED);
    const int evening = s.index_of(HistoryWorld::EVENING);

    s.tap_action(evening, "copyAction");
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);

    CHECK(s.notice_title() == QStringLiteral("The log was not shared"));
    CHECK(s.status(evening) == QStringLiteral("Not shared"));
}

TEST_CASE("history.reshare: a share that is cancelled says nothing", "[ui][history]")
{
    Screen s;
    s.m_World.platform().answer_with(ShareOutcome::CANCELLED);
    const int evening = s.index_of(HistoryWorld::EVENING);

    s.tap_action(evening, "copyAction");
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);

    CHECK_FALSE(s.visible("historyNotice"));
    CHECK(s.status(evening) == QStringLiteral("Not shared"));
}

TEST_CASE("history.reshare: Share log again on the open past log shares that log and updates its row", "[ui][history]")
{
    Screen s;
    s.open(HistoryWorld::MORNING);

    s.m_Rig.tap(s.named("reviewShare"));
    s.m_Rig.settle();
    s.m_Rig.advance(100ms);

    REQUIRE(s.m_World.platform().sheets_presented() == 1);
    test::check_golden("prd-example", s.m_World.platform().shared_texts().front());
    // The page and the row both show the new time.
    CHECK(s.text("reviewShared") == QStringLiteral("Shared 7 Nov 2026 at 19:10"));
    s.m_Rig.tap(s.named("reviewBack"));
    s.m_Rig.advance(700ms);
    s.m_Rig.settle();
    CHECK(s.status(s.index_of(HistoryWorld::MORNING)) == QStringLiteral("Shared 19:10"));
}

TEST_CASE("history.reshare: a platform that cannot share offers no Share on a row", "[ui][history]")
{
    Screen s(false);
    s.swipe_open(0);
    QQuickItem* share = s.named("copyAction", s.row(0));
    REQUIRE(share != nullptr);
    CHECK_FALSE(share->isVisible());
    CHECK(s.required("deleteAction", s.row(0))->isVisible());
}

TEST_CASE("history.delete: Delete asks for confirmation naming the date, the place and the entries", "[ui][history]")
{
    Screen s;
    const int morning = s.index_of(HistoryWorld::MORNING);
    CHECK_FALSE(s.alert_open());

    s.tap_action(morning, "deleteAction");
    s.m_Rig.advance(500ms);

    REQUIRE(s.alert_open());
    CHECK(s.text("alertTitle") == QStringLiteral("Delete this log?"));
    CHECK(s.text("alertMessage") ==
          QStringLiteral(
              "Sat 7 Nov, UNIT-27 at North Site, 6 entries. It is removed from this phone and can’t be brought back."));
    // Nothing has been deleted yet.
    CHECK(s.m_World.stored(HistoryWorld::MORNING));
    CHECK(s.list_count() == 4);
    CHECK(s.required("alertConfirm")->property("label").toString() == QStringLiteral("Delete log"));
}

TEST_CASE("history.delete: Cancel deletes nothing", "[ui][history]")
{
    Screen s;
    s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
    s.m_Rig.advance(500ms);
    REQUIRE(s.alert_open());

    s.m_Rig.tap(s.named("alertCancel"));
    s.m_Rig.settle();
    s.m_Rig.advance(600ms);

    CHECK_FALSE(s.alert_open());
    CHECK(s.m_World.stored(HistoryWorld::MORNING));
    CHECK(s.list_count() == 4);

    // The row can be deleted afterwards: asking again gives a new token.
    s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
    s.m_Rig.advance(500ms);
    CHECK(s.alert_open());
}

TEST_CASE("history.delete: Delete log calls confirmDelete once and the row goes after the commit", "[ui][history]")
{
    Screen s;
    QSignalSpy finished(&s.m_World.history(), &viewmodel::HistoryModel::deleteFinished);
    QSignalSpy announced(s.m_History, SIGNAL(announced(QString)));
    s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
    s.m_Rig.advance(500ms);
    REQUIRE(s.alert_open());

    s.m_Rig.tap(s.named("alertConfirm"));
    // Submitted, not yet committed: the row is still there.
    CHECK(s.list_count() == 4);
    s.m_Rig.settle();
    s.m_Rig.advance(600ms);

    REQUIRE(finished.count() == 1);
    CHECK(finished.at(0).at(0).toString() == log_text(HistoryWorld::MORNING));
    CHECK(finished.at(0).at(1).toBool());
    CHECK_FALSE(s.alert_open());
    CHECK_FALSE(s.m_World.stored(HistoryWorld::MORNING));
    CHECK(s.list_count() == 3);
    CHECK(s.index_of(HistoryWorld::MORNING) == -1);
    CHECK_FALSE(s.visible("historyNotice"));
    REQUIRE(announced.count() == 1);
    CHECK(announced.at(0).at(0).toString() == QStringLiteral("Log deleted"));
    CHECK(s.m_World.platform().haptics().empty());
}

TEST_CASE("history.delete: a failed delete keeps the row and says why", "[ui][history]")
{
    Screen s;
    s.m_World.storage().repository().fail_next(RepositoryOp::HARD_DELETE_LOG);
    s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
    s.m_Rig.advance(500ms);
    s.m_Rig.tap(s.named("alertConfirm"));
    s.m_Rig.settle();
    s.m_Rig.advance(600ms);

    CHECK(s.m_World.stored(HistoryWorld::MORNING));
    CHECK(s.list_count() == 4);
    CHECK(s.notice_title() == QStringLiteral("The log was not deleted"));
    CHECK(s.text("bannerMessage", s.named("historyNotice")) ==
          QStringLiteral("Nothing was changed. It is still in History."));

    // Trying again works.
    s.m_Rig.tap(s.named("bannerClose", s.named("historyNotice")));
    s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
    s.m_Rig.advance(500ms);
    s.m_Rig.tap(s.named("alertConfirm"));
    s.m_Rig.settle();
    s.m_Rig.advance(600ms);
    CHECK_FALSE(s.m_World.stored(HistoryWorld::MORNING));
    CHECK(s.list_count() == 3);
}

TEST_CASE("history.delete: leaving the tab while the question is up withdraws it", "[ui][history]")
{
    Screen s;
    s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
    s.m_Rig.advance(500ms);
    REQUIRE(s.alert_open());

    s.hidden();

    CHECK_FALSE(s.alert_open());
    CHECK(s.m_World.stored(HistoryWorld::MORNING));
}

TEST_CASE("history.cross_cutting: names, targets, order and text scale, in light and dark", "[ui][history]")
{
    Screen s;
    expect_clean(check_everything(s.m_Rig));
    s.m_Rig.set_dark(true);
    expect_clean(check_everything(s.m_Rig));
    s.m_Rig.set_dark(false);

    SECTION("with the banner over the bottom of the list")
    {
        s.m_World.storage().repository().fail_next(RepositoryOp::LIST_LOGS);
        s.shown();
        REQUIRE(s.visible("historyNotice"));
        expect_clean(check_everything(s.m_Rig));
    }

    SECTION("with a row swiped open")
    {
        // The row's face has slid left, partly out of the window, which is what a swipe is; the buttons it has
        // uncovered are named and big enough.
        s.swipe_open(0);
        expect_clean(check_names(s.m_Rig));
        expect_clean(check_targets(s.m_Rig));
    }

    SECTION("with the past log open")
    {
        s.open(HistoryWorld::MORNING);
        expect_clean(check_everything(s.m_Rig));
        s.m_Rig.set_dark(true);
        expect_clean(check_everything(s.m_Rig));
    }

    SECTION("with the confirmation up")
    {
        s.tap_action(s.index_of(HistoryWorld::MORNING), "deleteAction");
        s.m_Rig.advance(500ms);
        REQUIRE(s.alert_open());
        // The page under the alert is disabled but still in the tree; the reading order of the alert is what is
        // checked.
        s.required("historyPage")->setVisible(false);
        expect_clean(check_everything(s.m_Rig));
        s.m_Rig.set_dark(true);
        expect_clean(check_everything(s.m_Rig));
    }

    SECTION("nothing interactive is in the insets of an iPhone 17, nor in those of a taller phone")
    {
        s.m_History->setProperty("overrideInsets", true);
        for (const auto& insets : {std::pair{59.0, 34.0}, std::pair{79.0, 54.0}})
        {
            s.m_History->setProperty("overrideTop", insets.first);
            s.m_History->setProperty("overrideBottom", insets.second);
            s.m_Rig.layout();
            expect_clean(check_safe_area(s.m_Rig, insets.first, insets.second));
        }
    }
}

TEST_CASE("history.cross_cutting: with Reduce Motion the page fades in 100 ms and nothing slides",
          "[ui][history][motion]")
{
    Screen s;
    s.m_Rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(s.m_Rig));

    const int morning = s.index_of(HistoryWorld::MORNING);
    s.m_Rig.tap(s.named("face", s.row(morning)));
    s.m_Rig.settle();
    s.m_Rig.advance(150ms);
    CHECK(s.review_shown());
    CHECK(s.visible("reviewList"));
    expect_clean(check_motion_reduced(s.m_Rig));

    s.m_Rig.tap(s.named("reviewBack"));
    s.m_Rig.advance(150ms);
    s.m_Rig.settle();
    CHECK_FALSE(s.review_shown());
    CHECK(s.m_World.history().review_model() == nullptr);
}
