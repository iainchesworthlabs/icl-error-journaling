// SPDX-License-Identifier: GPL-3.0-or-later
// The filter of the Review screen (docs/plan/feedback-log-filter.md): review.filter_row, review.filter_kinds,
// review.filter_no_match, review.filter_whole_log and review.filter_past_log.
#include "review_test_support.hpp"
// The other headers of the tests come after the project's, as the support header says.
#include "golden.hpp"
#include "input_components_support.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QMetaObject>
#include <QQuickItem>
#include <QString>
#include <QVariant>
#include <vector>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::review;
using ej::viewmodel::TimelineFilterModel;

namespace
{

[[nodiscard]] bool shown(Screen& s, const char* name)
{
    QQuickItem* item = s.named(name);
    return item != nullptr && item->isVisible();
}

/// The Filter button of the bar: opens the kinds and the field under the header block, or closes them.
void toggle_filter(Screen& s)
{
    REQUIRE(s.named("reviewFilterButton") != nullptr);
    s.m_Rig.tap(s.named("reviewFilterButton"));
}

[[nodiscard]] TimelineFilterModel& filter_of(Screen& s)
{
    return *s.m_World.timeline().filtered();
}

} // namespace

TEST_CASE("review.filter_row: a button in the bar opens the filter, and a log with no filter has no extra row",
          "[ui][review][filter]")
{
    Screen s;
    CHECK(shown(s, "reviewFilterButton"));
    CHECK_FALSE(shown(s, "reviewFilter"));
    CHECK_FALSE(shown(s, "reviewFilterLabel"));
    CHECK(row_lines(s.m_Rig).size() == 6);

    toggle_filter(s);
    CHECK(shown(s, "reviewFilter"));
    CHECK(shown(s, "reviewFilterKinds"));
    CHECK(shown(s, "reviewFilterField"));
    CHECK(shown(s, "reviewFilterKind_error"));
    CHECK(shown(s, "reviewFilterKind_note"));
    CHECK(shown(s, "reviewFilterKind_header_change"));
    CHECK_FALSE(shown(s, "reviewFilterLabel"));

    toggle_filter(s);
    CHECK_FALSE(shown(s, "reviewFilter"));
}

TEST_CASE("review.filter_row: an empty log has no filter", "[ui][review][filter]")
{
    Options options;
    options.entries = std::vector<Entry>{};
    Screen s(options);
    CHECK_FALSE(shown(s, "reviewFilterButton"));
}

TEST_CASE("review.filter_kinds: a kind chip shows only those rows and says how many", "[ui][review][filter]")
{
    Screen s;
    toggle_filter(s);

    s.m_Rig.tap(s.named("reviewFilterKind_error"));
    CHECK(filter_of(s).shown_count() == 4);
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewFilterLabel")) == QStringLiteral("Filter · 4 of 6"));
    CHECK(shown(s, "reviewFilterClear"));
    CHECK(shown(s, "reviewFilterNote"));
    CHECK(s.named("reviewFilterKind_error")->property("checked").toBool());
    CHECK_FALSE(s.named("reviewFilterKind_note")->property("checked").toBool());

    // With the kinds closed the filter is still on, and the rows are the errors.
    toggle_filter(s);
    CHECK(shown(s, "reviewFilterLabel"));
    const std::vector<QString> lines = row_lines(s.m_Rig);
    REQUIRE(lines.size() == 4);
    for (const QString& line : lines)
    {
        CHECK(line.contains(QStringLiteral("T| ")));
    }

    // A second kind adds its rows; the same chip again takes them off.
    toggle_filter(s);
    s.m_Rig.tap(s.named("reviewFilterKind_note"));
    CHECK(filter_of(s).shown_count() == 5);
    s.m_Rig.tap(s.named("reviewFilterKind_error"));
    CHECK(filter_of(s).shown_count() == 1);

    // Clear puts every row back, and the status row goes.
    s.m_Rig.tap(s.named("reviewFilterClear"));
    CHECK(filter_of(s).shown_count() == 6);
    CHECK_FALSE(filter_of(s).active());
    CHECK_FALSE(shown(s, "reviewFilterLabel"));
    toggle_filter(s);
    CHECK(row_lines(s.m_Rig).size() == 6);
}

TEST_CASE("review.filter_kinds: the Observations chip is there for a log that has one or a phone with the board on",
          "[ui][review][filter]")
{
    SECTION("neither: no chip")
    {
        Screen s;
        toggle_filter(s);
        CHECK_FALSE(shown(s, "reviewFilterKind_interaction"));
    }
    SECTION("the second board is on: the chip is shown before there is an observation")
    {
        Screen s;
        s.m_Review->setProperty("observationsOn", true);
        toggle_filter(s);
        CHECK(shown(s, "reviewFilterKind_interaction"));
    }
}

TEST_CASE("review.filter_words: words typed in the field filter the rows, and the clear button empties it",
          "[ui][review][filter]")
{
    Screen s;
    toggle_filter(s);
    QQuickItem* input = s.named("reviewFilterInput");
    REQUIRE(input != nullptr);
    CHECK(shown(s, "reviewFilterPlaceholder"));

    // What the model holds is what the field shows, and the other way round.
    filter_of(s).set_text(QStringLiteral("timed out"));
    s.m_Rig.layout();
    CHECK(input->property("text").toString() == QStringLiteral("timed out"));
    CHECK(filter_of(s).shown_count() == 1);
    CHECK_FALSE(shown(s, "reviewFilterPlaceholder"));

    s.m_Rig.tap(s.named("reviewFilterFieldClear"));
    CHECK(filter_of(s).text().isEmpty());
    CHECK(filter_of(s).shown_count() == 6);
    CHECK(shown(s, "reviewFilterPlaceholder"));
}

TEST_CASE("review.filter_no_match: words that match nothing say so, and Clear filter brings the rows back",
          "[ui][review][filter]")
{
    Screen s;
    filter_of(s).set_text(QStringLiteral("no such words"));
    s.m_Rig.layout();

    CHECK(row_lines(s.m_Rig).empty());
    CHECK(shown(s, "reviewNoMatch"));
    CHECK_FALSE(shown(s, "reviewHint"));
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewFilterLabel")) == QStringLiteral("Filter · 0 of 6"));

    s.m_Rig.tap(s.named("reviewNoMatchClear"));
    CHECK_FALSE(filter_of(s).active());
    CHECK(row_lines(s.m_Rig).size() == 6);
    CHECK_FALSE(shown(s, "reviewNoMatch"));
}

TEST_CASE("review.filter_whole_log: Copy log, the title and the count stay the whole log's while a filter is on",
          "[ui][review][filter]")
{
    Screen s;
    const QString title = text_in(s.m_Rig, QStringLiteral("reviewSummary"));
    filter_of(s).toggleKind(QStringLiteral("note"));
    s.m_Rig.layout();
    REQUIRE(filter_of(s).shown_count() == 1);

    s.m_Rig.tap(s.named("reviewCopyAll"));
    test::check_golden("prd-example", s.m_World.clipboard().text());
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewSummary")) == title);
    CHECK(s.m_World.timeline().entry_count() == 6);
}

TEST_CASE("review.filter_whole_log: Copy line on a filtered row copies that row's line", "[ui][review][filter]")
{
    Screen s;
    filter_of(s).toggleKind(QStringLiteral("header_change"));
    s.m_Rig.layout();
    const std::vector<QString> lines = row_lines(s.m_Rig);
    REQUIRE(lines.size() == 1);
    INFO(lines.front().toStdString());
    CHECK(lines.front().contains(QStringLiteral("Software Version")));

    REQUIRE(QMetaObject::invokeMethod(s.m_Review, "copyRow", Q_ARG(int, 0)));
    CHECK(QString::fromStdString(s.m_World.clipboard().text()).contains(QStringLiteral("Software Version")));
}

TEST_CASE("review.filter_past_log: a past log has the same filter, and it is its own", "[ui][review][filter]")
{
    Options options;
    options.past = true;
    Screen s(options);
    CHECK(shown(s, "reviewFilterButton"));
    toggle_filter(s);
    s.m_Rig.tap(s.named("reviewFilterKind_error"));
    // The past log is the timeline of the screen; the open log's filter is not touched.
    CHECK_FALSE(filter_of(s).active());
    CHECK(shown(s, "reviewFilterLabel"));
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewFilterLabel")) == QStringLiteral("Filter · 4 of 6"));
}
