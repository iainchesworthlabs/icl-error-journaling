// SPDX-License-Identifier: GPL-3.0-or-later
// Review (docs/plan/stage-4.md, 4.10): review.header_and_rows, review.row_wording, review.copy, review.delete_swipe,
// review.share, review.empty, review.past_log, review.voiceover, review.long_log and the cross-cutting checks. The Edit
// entry sheet has its own file, edit_entry_test.cpp.
#include "review_test_support.hpp"
// The other headers of the tests come after the project's, as the support header says.
#include "golden.hpp"
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QColor>
#include <QMetaObject>
#include <QQuickItem>
#include <QSignalSpy>
#include <QString>
#include <algorithm>
#include <chrono>
#include <set>
#include <utility>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::review;
using ej::bridge::ShareOutcome;
using ej::viewmodel::TimelineModel;
using namespace std::chrono_literals;

namespace
{

/// The colours the Review text and surfaces may have: black and white (the review tokens) and the neutral divider.
std::set<QRgb> neutral_colours(UiRig& rig)
{
    std::set<QRgb> allowed;
    for (const char* name :
         {"reviewBackground", "reviewSurface", "reviewText", "reviewTextSecondary", "reviewDivider", "divider"})
    {
        allowed.insert(input::theme_colour(rig, name).rgba());
    }
    // Black and white themselves, which is also what an unset border colour is.
    allowed.insert(QColor(Qt::black).rgba());
    allowed.insert(QColor(Qt::white).rgba());
    return allowed;
}

/// The faults of a list that has a colour of its own anywhere: a Rectangle's fill and border, a Text's colour. The
/// strips behind a row (Copy, Delete) are the only coloured things and are not in the list at rest; they are skipped.
std::vector<QString> coloured_items(UiRig& rig, QQuickItem* list)
{
    const std::set<QRgb> allowed = neutral_colours(rig);
    std::vector<QQuickItem*> strips = all_named(rig, QStringLiteral("copyAction"), list);
    for (QQuickItem* strip : all_named(rig, QStringLiteral("deleteAction"), list))
    {
        strips.push_back(strip);
    }
    const auto in_strip = [&strips](const QQuickItem* item)
    { return std::ranges::any_of(strips, [item](const QQuickItem* strip) { return strip->isAncestorOf(item); }); };
    std::vector<QString> faults;
    for (const Node& node : rig.tree(list))
    {
        if (in_strip(node.m_Item))
        {
            continue;
        }
        const auto check = [&](const char* what, const QColor& colour)
        {
            if (colour.alpha() != 0 && !allowed.contains(colour.rgba()))
            {
                faults.push_back(QStringLiteral("%1 of %2 %3 is %4")
                                     .arg(QLatin1String(what), node.m_TypeName, node.m_ObjectName, colour.name()));
            }
        };
        if (node.m_TypeName.startsWith(QLatin1String("QQuickRectangle")))
        {
            check("the fill", node.m_Item->property("color").value<QColor>());
            const QObject* border = input::require_object(node.m_Item, "border");
            if (border->property("width").toDouble() > 0)
            {
                check("the border", border->property("color").value<QColor>());
            }
        }
        else if (node.m_TypeName.startsWith(QLatin1String("QQuickText")))
        {
            check("the text", node.m_Item->property("color").value<QColor>());
        }
    }
    return faults;
}

QString export_line(const TimelineModel& model, int row)
{
    return model.data(model.index(row), TimelineModel::FULL_LINE).toString();
}

} // namespace

TEST_CASE("review.header_and_rows: the header block is headerText and each row is timeText and lineText",
          "[ui][review]")
{
    Screen s;
    const TimelineModel& timeline = s.m_World.timeline();

    CHECK(text_in(s.m_Rig, QStringLiteral("reviewHeaderText")) == timeline.header_text());
    CHECK(timeline.header_text() ==
          QStringLiteral("Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #: UNIT-27\nLocation: North Site\n"
                         "Software Version: 1.4.2"));
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewSummary")) == timeline.title_text());
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewTitle")) == QStringLiteral("Today’s log"));

    const std::vector<QString> shown = row_lines(s.m_Rig);
    REQUIRE(shown.size() == 6);
    for (int i = 0; i < 6; ++i)
    {
        CHECK(shown[static_cast<std::size_t>(i)] == export_line(timeline, i));
    }

    SECTION("only black and white: no coloured fill, border or text in the list, in light and dark")
    {
        QQuickItem* list = s.named("reviewList");
        REQUIRE(list != nullptr);
        INFO(describe(coloured_items(s.m_Rig, list)).toStdString());
        CHECK(coloured_items(s.m_Rig, list).empty());

        s.m_Rig.set_dark(true);
        CHECK(coloured_items(s.m_Rig, list).empty());
        // The dark version is white on black.
        QQuickItem* line = s.named("reviewLine");
        REQUIRE(line != nullptr);
        CHECK(line->property("color").value<QColor>() == QColor(Qt::white));
        CHECK(input::theme_colour(s.m_Rig, "reviewBackground") == QColor(Qt::black));
        CHECK(input::fill_of(s.m_Review) == QColor(Qt::black));
    }

    SECTION("the header block opens Today's details on the open log")
    {
        QSignalSpy requested(s.m_Review, SIGNAL(headerRequested()));
        s.m_Rig.tap(s.named("reviewHeader"));
        CHECK(requested.count() == 1);
    }
}

TEST_CASE("review.row_wording: every row of the PRD example equals the golden line", "[ui][review]")
{
    Screen s;
    CHECK(joined(row_lines(s.m_Rig)) == joined(prd_example_lines()));
    // The golden file is the export, so the lines are the ones Share sends.
    s.m_Rig.tap(s.named("reviewCopyAll"));
    test::check_golden("prd-example", s.m_World.clipboard().text());
}

TEST_CASE("review.copy: Copy-all and Copy put the export and the line on the clipboard, and say so", "[ui][review]")
{
    Screen s;
    QSignalSpy announced(s.m_Review, SIGNAL(announced(QString)));
    QSignalSpy recorded(&s.m_World.share(), SIGNAL(shareRecorded(QString)));

    SECTION("Copy-all in the bar copies the whole log once")
    {
        QQuickItem* copy_all = s.named("reviewCopyAll");
        REQUIRE(copy_all != nullptr);
        CHECK(copy_all->isEnabled());
        s.m_Rig.tap(copy_all);
        CHECK(s.m_World.clipboard().copies() == 1);
        test::check_golden("prd-example", s.m_World.clipboard().text());

        // "Copied" is shown and announced once, and goes away by itself.
        REQUIRE(announced.count() == 1);
        CHECK(announced.at(0).at(0).toString() == QStringLiteral("Copied the whole log"));
        CHECK(s.visible("reviewCopied"));
        CHECK(input::require_interface(s.item("reviewCopied"))->text(QAccessible::Name) ==
              QStringLiteral("Copied the whole log"));
        QObject* timer = s.m_Review->findChild<QObject*>(QStringLiteral("copiedTimer"));
        REQUIRE(timer != nullptr);
        CHECK(timer->property("running").toBool());
        QMetaObject::invokeMethod(timer, "triggered");
        s.m_Rig.layout();
        CHECK_FALSE(s.visible("reviewCopied"));
    }

    SECTION("Copy on a swiped row copies that line and nothing else")
    {
        s.tap_action(3, "copyAction");
        CHECK(s.m_World.clipboard().copies() == 1);
        CHECK(QString::fromStdString(s.m_World.clipboard().text()) == prd_example_lines()[3]);
        REQUIRE(announced.count() == 1);
        CHECK(announced.at(0).at(0).toString() == QStringLiteral("Copied the line"));
        // The row is still there and the log is untouched.
        s.commit();
        CHECK(joined(row_lines(s.m_Rig)) == joined(prd_example_lines()));
    }

    // Neither copy marks the log as shared (Stage 3, decision 10).
    CHECK(recorded.count() == 0);
    CHECK(s.m_World.platform().sheets_presented() == 0);
}

TEST_CASE("review.delete_swipe: Delete removes the row after the commit, and the toast's Undo brings it back",
          "[ui][review]")
{
    Screen s;
    REQUIRE(joined(row_lines(s.m_Rig)) == joined(prd_example_lines()));

    s.tap_action(1, "deleteAction");
    // Nothing leaves before the commit.
    CHECK(row_lines(s.m_Rig).size() == 6);
    CHECK_FALSE(s.m_World.undo().active());
    s.commit();

    std::vector<QString> expected = prd_example_lines();
    expected.erase(expected.begin() + 1);
    CHECK(joined(row_lines(s.m_Rig)) == joined(expected));
    // The toast is the UndoController's: the screen has no toast of its own.
    CHECK(s.m_World.undo().active());
    CHECK(s.m_World.undo().text() == QStringLiteral("Entry deleted"));
    CHECK(s.m_World.timeline().entry_count() == 5);

    REQUIRE(s.m_World.undo().undo());
    s.commit();
    CHECK(joined(row_lines(s.m_Rig)) == joined(prd_example_lines()));
    CHECK_FALSE(s.m_World.undo().active());
    CHECK(s.m_World.platform().haptics().empty());
}

TEST_CASE("review.share: Share log calls shareOpenLog once, is disabled while sharing and never fires a haptic",
          "[ui][review]")
{
    SECTION("the button is absent when the platform has no share sheet")
    {
        Screen s(Options{.share_available = false});
        CHECK_FALSE(s.visible("reviewShare"));
    }

    SECTION("it shares the open log")
    {
        Screen s;
        QQuickItem* button = s.named("reviewShare");
        REQUIRE(button != nullptr);
        CHECK(button->isVisible());
        CHECK(button->property("text").toString() == QStringLiteral("Share log"));
        s.m_Rig.tap(button);
        s.m_Rig.settle();
        REQUIRE(s.m_World.platform().sheets_presented() == 1);
        test::check_golden("prd-example", s.m_World.platform().shared_texts().front());
        CHECK(s.m_World.platform().haptics().empty());
        CHECK_FALSE(s.visible("reviewNotice"));
    }

    SECTION("it is disabled while a share is under way, and a cancelled share says nothing")
    {
        Screen s;
        s.m_World.platform().hold_completion();
        QQuickItem* button = s.named("reviewShare");
        s.m_Rig.tap(button);
        REQUIRE(s.m_World.share().sharing());
        CHECK_FALSE(button->isEnabled());
        s.m_Rig.tap(button);
        CHECK(s.m_World.platform().sheets_presented() == 1);

        REQUIRE(s.m_World.platform().complete_share_sheet(ShareOutcome::CANCELLED));
        s.m_Rig.settle();
        CHECK_FALSE(s.m_World.share().sharing());
        CHECK(button->isEnabled());
        CHECK_FALSE(s.visible("reviewNotice"));
    }

    SECTION("a failed share shows a banner, which can be closed")
    {
        Screen s;
        s.m_World.platform().answer_with(ShareOutcome::FAILED);
        s.m_Rig.tap(s.named("reviewShare"));
        s.m_Rig.settle();
        s.m_Rig.layout();
        REQUIRE(s.visible("reviewNotice"));
        CHECK(text_in(s.m_Rig, QStringLiteral("bannerTitle"), s.named("reviewNotice")) ==
              QStringLiteral("The log was not shared"));
        s.m_Rig.tap(s.named("bannerClose", s.named("reviewNotice")));
        s.m_Rig.layout();
        CHECK_FALSE(s.visible("reviewNotice"));
        CHECK(s.m_World.platform().haptics().empty());
    }
}

TEST_CASE("review.empty: an empty log says so and disables Share and Copy-all", "[ui][review]")
{
    Screen s(Options{.entries = std::vector<Entry>{}});
    CHECK(row_lines(s.m_Rig).empty());
    CHECK(s.visible("reviewEmpty"));
    CHECK_FALSE(s.item("reviewCopyAll")->isEnabled());
    CHECK_FALSE(s.item("reviewShare")->isEnabled());

    s.m_Rig.tap(s.named("reviewCopyAll"));
    s.m_Rig.tap(s.named("reviewShare"));
    CHECK(s.m_World.clipboard().copies() == 0);
    CHECK(s.m_World.platform().sheets_presented() == 0);

    // The header block is still there.
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewHeaderText")) == s.m_World.timeline().header_text());
    expect_clean(check_everything(s.m_Rig));
}

TEST_CASE("review.past_log: any TimelineModel, with a back control, the date shared and Share log again",
          "[ui][review]")
{
    Screen s(Options{.past = true});
    QSignalSpy back(s.m_Review, SIGNAL(backRequested()));
    QSignalSpy header(s.m_Review, SIGNAL(headerRequested()));

    CHECK(joined(row_lines(s.m_Rig)) == joined(prd_example_lines()));
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewTitle")) == QStringLiteral("Thu 5 Nov"));
    CHECK(text_in(s.m_Rig, QStringLiteral("reviewShared")) == QStringLiteral("Shared 5 Nov at 16:58"));
    CHECK(s.item("reviewShare")->property("text").toString() == QStringLiteral("Share log again"));

    // The back control replaces the summary and goes back.
    QQuickItem* back_button = s.named("reviewBack");
    REQUIRE(back_button != nullptr);
    CHECK(back_button->isVisible());
    CHECK_FALSE(s.visible("reviewSummary"));
    s.m_Rig.tap(back_button);
    CHECK(back.count() == 1);

    // The header block of a past log is not a link.
    s.m_Rig.tap(s.named("reviewHeader"));
    CHECK(header.count() == 0);

    // Share log again shares that log, not the open one.
    s.m_Rig.tap(s.named("reviewShare"));
    s.m_Rig.settle();
    REQUIRE(s.m_World.platform().sheets_presented() == 1);
    test::check_golden("prd-example", s.m_World.platform().shared_texts().front());

    // Copy-all copies the past log.
    s.m_Rig.tap(s.named("reviewCopyAll"));
    test::check_golden("prd-example", s.m_World.clipboard().text());

    s.m_Rig.layout();
    expect_clean(check_everything(s.m_Rig));
    s.m_Rig.set_dark(true);
    expect_clean(check_everything(s.m_Rig));
}

TEST_CASE("review.past_log: without a share time nothing is said about one", "[ui][review]")
{
    Screen s(Options{.past = true});
    s.m_Review->setProperty("sharedTimeText", QString());
    s.m_Rig.layout();
    CHECK_FALSE(s.visible("reviewShared"));
}

TEST_CASE("review.voiceover: a row is one button reading the whole line and opening Edit", "[ui][review]")
{
    Screen s;
    const std::vector<QString> lines = prd_example_lines();
    for (int i = 0; i < 6; ++i)
    {
        QQuickItem* face = s.face(i);
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(face);
        REQUIRE(iface != nullptr);
        CHECK(iface->role() == QAccessible::Button);
        CHECK(iface->text(QAccessible::Name) == lines[static_cast<std::size_t>(i)]);

        // Copy and Delete are reachable without the swipe, named after the row.
        QStringList names;
        for (const Node& node : s.m_Rig.accessible_tree(s.row(i)))
        {
            if (node.m_Role == QAccessible::Button)
            {
                names.push_back(node.m_Name);
            }
        }
        CHECK(names == QStringList{lines[static_cast<std::size_t>(i)],
                                   QStringLiteral("Copy, ") + lines[static_cast<std::size_t>(i)],
                                   QStringLiteral("Delete, ") + lines[static_cast<std::size_t>(i)]});
    }

    // Activating the row opens the Edit sheet for it.
    QObject* editor = s.m_Review->property("editor").value<QObject*>();
    REQUIRE(editor != nullptr);
    CHECK_FALSE(editor->property("isOpen").toBool());
    input::press_action(s.face(2));
    CHECK(editor->property("isOpen").toBool());
    CHECK(editor->property("entryId").toString() == id_text(3));
}

TEST_CASE("review.long_log: 500 entries use a few delegates, which are reused while scrolling", "[ui][review][perf]")
{
    constexpr int ENTRIES = 500;
    constexpr std::size_t DELEGATE_BUDGET = 40;

    World world;
    std::vector<Entry> entries;
    for (int i = 0; i < ENTRIES; ++i)
    {
        entries.push_back(error_at(world.configuration(), static_cast<std::uint64_t>(i + 1), 8 + i / 60, i % 60));
    }
    world.open_log(entries);
    UiRig rig;
    rig.attach(world.clock());
    rig.attach(world.gateway());
    QQuickItem* review = rig.load(QStringLiteral("Review"), world.properties());
    INFO(rig.errors().toStdString());
    REQUIRE(review != nullptr);
    REQUIRE(world.timeline().entry_count() == ENTRIES);

    QQuickItem* list = find_in(rig, QStringLiteral("reviewList"));
    REQUIRE(list != nullptr);
    CHECK(all_named(rig, QStringLiteral("reviewRow")).size() < DELEGATE_BUDGET);
    CHECK(list->property("count").toInt() == ENTRIES);

    // Scroll to the end in screens: the delegates are reused, so the count stays small.
    std::size_t most = 0;
    for (int screen = 0; screen < 20; ++screen)
    {
        list->setProperty("contentY", list->property("contentY").toDouble() + list->height());
        rig.layout();
        most = std::max(most, all_named(rig, QStringLiteral("reviewRow")).size());
    }
    CHECK(list->property("contentY").toDouble() > list->height());
    CHECK(most < DELEGATE_BUDGET);

    QMetaObject::invokeMethod(list, "positionViewAtEnd");
    rig.layout();
    const std::vector<QString> lines = row_lines(rig);
    REQUIRE_FALSE(lines.empty());
    CHECK(lines.back().startsWith(QStringLiteral("16:19 ")));
}

TEST_CASE("review.cross_cutting: names, targets, order and text scale, in light, dark and with the notice",
          "[ui][review]")
{
    Screen s;
    expect_clean(check_everything(s.m_Rig));
    s.m_Rig.set_dark(true);
    expect_clean(check_everything(s.m_Rig));

    // With the banner and "Copied" over the bottom of the list.
    s.m_Rig.set_dark(false);
    s.m_World.platform().answer_with(ShareOutcome::FAILED);
    s.m_Rig.tap(s.named("reviewShare"));
    s.m_Rig.settle();
    s.m_Rig.tap(s.named("reviewCopyAll"));
    s.m_Rig.layout();
    REQUIRE(s.visible("reviewNotice"));
    REQUIRE(s.visible("reviewCopied"));
    expect_clean(check_everything(s.m_Rig));

    // Nothing interactive in the insets of an iPhone 17, nor in those of a taller phone.
    s.m_Review->setProperty("overrideInsets", true);
    for (const auto& insets : {std::pair{59.0, 34.0}, std::pair{79.0, 54.0}})
    {
        s.m_Review->setProperty("overrideTop", insets.first);
        s.m_Review->setProperty("overrideBottom", insets.second);
        s.m_Rig.layout();
        expect_clean(check_safe_area(s.m_Rig, insets.first, insets.second));
    }
}

TEST_CASE("review.cross_cutting: with Reduce Motion a swiped row jumps and nothing slides", "[ui][review]")
{
    Screen s;
    s.m_Rig.set_reduce_motion(true);
    s.swipe_open(0);
    expect_clean(check_motion_reduced(s.m_Rig));
}
