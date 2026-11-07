// SPDX-License-Identifier: GPL-3.0-or-later
// HeaderScreen, Today's details (docs/plan/stage-4.md, 4.9): details.fields, details.unit_keypad, details.location,
// details.version_validation, details.correct_first_fill, details.new_log_row, details.keyboard and the cross-cutting
// checks. The models are the real ones of Stage 3 over the in-memory store.
#include "header_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"
#include "support/recording_repository.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::header;
using ej::test::RepositoryOp;
using namespace std::chrono_literals;

namespace
{

// Tall enough that nothing needs scrolling to be tapped; the accessibility checks use the real window.
const QSize TALL_WINDOW(402, 1600);

QQuickItem* location_row(UiRig& rig, const QString& name)
{
    for (const Node& node : rig.tree())
    {
        if (node.m_ObjectName == QStringLiteral("headerLocation") && node.m_Item->property("text").toString() == name)
        {
            return node.m_Item;
        }
    }
    FAIL("no location row named " << name.toStdString());
    return nullptr;
}

QQuickItem* text_item(UiRig& rig, const QString& text)
{
    for (const Node& node : rig.tree())
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) &&
            node.m_Item->property("text").toString() == text)
        {
            return node.m_Item;
        }
    }
    return nullptr;
}

QString input_text(UiRig& rig, const char* field)
{
    QQuickItem* input = input_of(rig, need(rig, QString::fromLatin1(field)));
    return input != nullptr ? input->property("text").toString() : QString();
}

QQuickItem* load_screen(UiRig& rig, HeaderWorld& world)
{
    world.attach(rig);
    QQuickItem* screen = rig.load(QStringLiteral("HeaderScreen"), world.models());
    INFO(rig.errors().toStdString());
    REQUIRE(screen != nullptr);
    rig.settle();
    return screen;
}

} // namespace

TEST_CASE("details.fields: the values come from the session; the facilitator and the date are not editable",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);

    QQuickItem* facilitator = need(rig, QStringLiteral("headerFacilitator"));
    QQuickItem* date = need(rig, QStringLiteral("headerDate"));
    REQUIRE(facilitator != nullptr);
    REQUIRE(date != nullptr);
    CHECK(facilitator->property("value").toString() == QStringLiteral("Sam Example"));
    CHECK(date->property("value").toString() == QStringLiteral("Sat 7 Nov"));
    for (QQuickItem* row : {facilitator, date})
    {
        CHECK_FALSE(row->property("tappable").toBool());
        for (const Node& node : rig.accessible_tree(row))
        {
            CHECK(node.m_Role != static_cast<int>(QAccessible::EditableText));
        }
    }
    const Node* row_node = nullptr;
    const auto nodes = rig.tree();
    for (const Node& node : nodes)
    {
        if (node.m_Item == facilitator)
        {
            row_node = &node;
        }
    }
    REQUIRE(row_node != nullptr);
    CHECK(row_node->m_Role == static_cast<int>(QAccessible::StaticText));

    CHECK(input_text(rig, "headerUnit") == QStringLiteral("27"));
    CHECK(input_text(rig, "headerVersion") == QStringLiteral("1.4.2"));
    CHECK(location_row(rig, QStringLiteral("North Site"))->property("checked").toBool());
    CHECK_FALSE(location_row(rig, QStringLiteral("South Site"))->property("checked").toBool());
    CHECK_FALSE(need(rig, QStringLiteral("headerOtherText"))->isVisible());
}

TEST_CASE("details.unit_keypad: the number pad, the prefix that is shown and not typed, and a refused value",
          "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* unit = need(rig, QStringLiteral("headerUnit"));
    REQUIRE(unit != nullptr);
    QQuickItem* input = input_of(rig, unit);
    REQUIRE(input != nullptr);

    SECTION("the number pad is asked for and the prefix is drawn apart from the field")
    {
        CHECK((input->property("inputMethodHints").toInt() & Qt::ImhDigitsOnly) != 0);
        CHECK(unit->property("prefixText").toString() == QStringLiteral("UNIT-"));
        CHECK(text_item(rig, QStringLiteral("UNIT-")) != nullptr);
        CHECK(input->property("text").toString().isEmpty());
        // Nothing is refused before the person has typed.
        CHECK(unit->property("errorText").toString().isEmpty());
    }

    SECTION("a leading zero shows the model's message and nothing is saved")
    {
        const auto before = world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER);
        retype(rig, unit, QStringLiteral("027"));
        const QString message =
            world.session().validateHeaderValue(QStringLiteral("unit_number"), QStringLiteral("027"));
        REQUIRE_FALSE(message.isEmpty());
        CHECK(unit->property("errorText").toString() == message);
        CHECK(world.session().unit_text().isEmpty());
        CHECK(world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER) == before);
        CHECK(world.session().pending_writes() == 0);
    }

    SECTION("letters are refused with the model's message and never reach the session")
    {
        retype(rig, unit, QStringLiteral("2a"));
        CHECK(unit->property("errorText").toString() ==
              world.session().validateHeaderValue(QStringLiteral("unit_number"), QStringLiteral("2a")));
        CHECK(world.session().unit_text().isEmpty());
    }

    SECTION("digits are saved as the unit number")
    {
        retype(rig, unit, QStringLiteral("27"));
        CHECK(unit->property("errorText").toString().isEmpty());
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
        CHECK(unit->property("valid").toBool());
        CHECK(world.timeline().entry_count() == 0);
    }
}

TEST_CASE("details.location: a configured location, and Other with a text", "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);

    SECTION("a configured location is chosen with a tap")
    {
        rig.tap(location_row(rig, QStringLiteral("South Site")));
        rig.settle();
        CHECK(world.session().location_text() == QStringLiteral("South Site"));
        CHECK(location_row(rig, QStringLiteral("South Site"))->property("checked").toBool());
        CHECK_FALSE(location_row(rig, QStringLiteral("North Site"))->property("checked").toBool());
        CHECK_FALSE(need(rig, QStringLiteral("headerOtherText"))->isVisible());
        CHECK(world.timeline().entry_count() == 0);
    }

    SECTION("Other shows a text field and sends what is typed")
    {
        rig.tap(need(rig, QStringLiteral("headerOther")));
        rig.settle();
        QQuickItem* other = need(rig, QStringLiteral("headerOtherText"));
        REQUIRE(other != nullptr);
        CHECK(other->isVisible());
        CHECK(need(rig, QStringLiteral("headerOther"))->property("checked").toBool());
        CHECK_FALSE(location_row(rig, QStringLiteral("North Site"))->property("checked").toBool());

        retype(rig, other, QStringLiteral("West Yard"));
        CHECK(world.session().location_text() == QStringLiteral("West Yard"));
        CHECK(other->property("errorText").toString().isEmpty());
    }

    SECTION("a location that is not configured opens as Other with its text")
    {
        rig.tap(need(rig, QStringLiteral("headerOther")));
        rig.settle();
        retype(rig, need(rig, QStringLiteral("headerOtherText")), QStringLiteral("West Yard"));

        UiRig second(TALL_WINDOW);
        world.attach(second);
        REQUIRE(second.load(QStringLiteral("HeaderScreen"), world.models()) != nullptr);
        second.settle();
        CHECK(need(second, QStringLiteral("headerOther"))->property("checked").toBool());
        CHECK(input_text(second, "headerOtherText") == QStringLiteral("West Yard"));
    }
}

TEST_CASE("details.version_validation: a refused version shows the message inline and is not saved",
          "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* version = need(rig, QStringLiteral("headerVersion"));
    REQUIRE(version != nullptr);
    const auto before = world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER);

    for (const char* refused : {"1.4", "1.4.2-beta"})
    {
        retype(rig, version, QString::fromLatin1(refused));
        const QString message =
            world.session().validateHeaderValue(QStringLiteral("software_version"), QString::fromLatin1(refused));
        REQUIRE_FALSE(message.isEmpty());
        CHECK(version->property("errorText").toString() == message);
        CHECK_FALSE(version->property("valid").toBool());
        CHECK(world.session().version_text().isEmpty());
    }
    CHECK(world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER) == before);

    retype(rig, version, QStringLiteral("1.4.2"));
    CHECK(version->property("errorText").toString().isEmpty());
    CHECK(version->property("valid").toBool());
    CHECK(world.session().version_text() == QStringLiteral("1.4.2"));
}

TEST_CASE("details.correct_first_fill: filling an empty field is a Correct and adds no entry", "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    REQUIRE(world.timeline().entry_count() == 0);
    REQUIRE(world.session().missing_fields().size() == 3);

    retype(rig, need(rig, QStringLiteral("headerUnit")), QStringLiteral("27"));
    retype(rig, need(rig, QStringLiteral("headerVersion")), QStringLiteral("1.4.2"));
    rig.tap(location_row(rig, QStringLiteral("North Site")));
    rig.settle();

    CHECK(world.session().missing_fields().isEmpty());
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
    CHECK(world.session().version_text() == QStringLiteral("1.4.2"));
    CHECK(world.session().location_text() == QStringLiteral("North Site"));
    CHECK(world.timeline().entry_count() == 0);
    CHECK(world.timeline().rowCount() == 0);
    CHECK(world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER) == 3);
    CHECK(world.storage().repository().call_count(RepositoryOp::ADD_ENTRY) == 0);
}

TEST_CASE("details.commit: a field is saved when the person is done with it, not on every key", "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    QQuickItem* screen = load_screen(rig, world);
    QQuickItem* unit = need(rig, QStringLiteral("headerUnit"));
    QQuickItem* version = need(rig, QStringLiteral("headerVersion"));
    const auto writes = [&world] { return world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER); };

    SECTION("typing writes nothing; leaving the field does")
    {
        rig.tap(input_of(rig, unit));
        rig.type(QStringLiteral("27"));
        rig.settle();
        CHECK(writes() == 0);
        CHECK(world.session().unit_text().isEmpty());
        rig.tap(input_of(rig, version));
        rig.settle();
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
        CHECK(writes() == 1);
    }

    SECTION("a version on its way to being valid is not saved on the way")
    {
        rig.tap(input_of(rig, version));
        rig.type(QStringLiteral("1.4.2"));
        rig.settle();
        CHECK(writes() == 0);
        rig.type(QStringLiteral("0"));
        rig.key(Qt::Key_Return);
        rig.settle();
        CHECK(writes() == 1);
        CHECK(world.session().version_text() == QStringLiteral("1.4.20"));
    }

    SECTION("Start logging saves what is valid and leaves the rest")
    {
        rig.tap(input_of(rig, unit));
        rig.type(QStringLiteral("27"));
        rig.tap(input_of(rig, version));
        rig.type(QStringLiteral("1.4"));
        QSignalSpy closed(screen, SIGNAL(closeRequested()));
        rig.tap(need(rig, QStringLiteral("headerStart")));
        rig.settle();
        CHECK(closed.count() == 1);
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
        CHECK(world.session().version_text().isEmpty());
        CHECK(writes() == 1);
    }

    SECTION("going back saves too")
    {
        rig.tap(input_of(rig, unit));
        rig.type(QStringLiteral("31"));
        rig.tap(need(rig, QStringLiteral("navLeading")));
        rig.settle();
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-31"));
    }
}

TEST_CASE("details.start_logging: leaving the screen needs no field", "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    QQuickItem* screen = load_screen(rig, world);
    QSignalSpy closed(screen, SIGNAL(closeRequested()));

    rig.tap(need(rig, QStringLiteral("headerStart")));
    CHECK(closed.count() == 1);
    rig.tap(need(rig, QStringLiteral("navLeading")));
    CHECK(closed.count() == 2);
    // Errors can be logged before every field is filled.
    CHECK(world.session().recordError(QStringLiteral("00000000-0000-4000-8000-0000000000a1")));
}

TEST_CASE("details.new_log_row: starting a new log asks first and then archives the open log", "[ui][header_screen]")
{
    HeaderWorld world(true);
    world.record_errors(2);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    REQUIRE(world.session().live_entry_count() == 2);
    QQuickItem* alert = need(rig, QStringLiteral("newLogAlert"));
    REQUIRE(alert != nullptr);
    QQuickItem* row = need(rig, QStringLiteral("headerNewLogRow"));
    REQUIRE(row != nullptr);
    CHECK(row->property("text").toString() == QStringLiteral("Start a new log for another unit or site"));
    CHECK_FALSE(alert->property("isOpen").toBool());

    SECTION("Cancel leaves the log as it is")
    {
        rig.tap(row);
        play(rig, 400);
        CHECK(alert->property("isOpen").toBool());
        // The session is not touched until the person agrees.
        CHECK(world.storage().repository().call_count(RepositoryOp::START_NEW_LOG) == 0);
        CHECK(alert->property("message").toString() ==
              QStringLiteral("UNIT-27 at North Site has 2 entries. It stays in History. The new log starts empty, with "
                             "Sam Example as facilitator."));
        rig.tap(need(rig, QStringLiteral("alertCancel"), alert));
        play(rig, 400);
        rig.settle();
        CHECK_FALSE(alert->property("isOpen").toBool());
        CHECK(world.storage().repository().call_count(RepositoryOp::START_NEW_LOG) == 0);
        CHECK(world.session().live_entry_count() == 2);
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
    }

    SECTION("Start a new log archives the open log and the fields are empty again")
    {
        rig.tap(row);
        play(rig, 400);
        rig.tap(need(rig, QStringLiteral("alertConfirm"), alert));
        play(rig, 400);
        rig.advance(100ms);
        rig.settle();
        rig.advance(100ms);
        rig.settle();

        CHECK(world.storage().repository().call_count(RepositoryOp::START_NEW_LOG) == 1);
        CHECK(world.session().live_entry_count() == 0);
        CHECK(world.session().unit_text().isEmpty());
        CHECK(world.session().facilitator() == QStringLiteral("Sam Example"));
        CHECK(input_text(rig, "headerUnit").isEmpty());
        CHECK(input_text(rig, "headerVersion").isEmpty());
        CHECK_FALSE(location_row(rig, QStringLiteral("North Site"))->property("checked").toBool());
    }
}

TEST_CASE("details.keyboard: the foot bar steps aside and the field being typed in stays in view",
          "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig;
    QQuickItem* screen = load_screen(rig, world);
    QQuickItem* scroller = need(rig, QStringLiteral("headerScroller"));
    QQuickItem* version = need(rig, QStringLiteral("headerVersion"));
    QQuickItem* start = need(rig, QStringLiteral("headerStart"));
    REQUIRE(scroller != nullptr);
    REQUIRE(version != nullptr);
    REQUIRE(start != nullptr);
    CHECK(start->isVisible());
    const double window_height = rig.window().height();

    rig.tap(input_of(rig, version));
    screen->setProperty("keyboardHeight", 336.0);
    play(rig, 64);

    const QRectF area = rig.geometry(scroller);
    const QRectF field = rig.geometry(version);
    CHECK(area.bottom() <= window_height - 336.0 + 0.5);
    CHECK_FALSE(start->isVisible());
    CHECK(field.top() >= area.top() - 0.5);
    CHECK(field.bottom() <= area.bottom() + 0.5);

    screen->setProperty("keyboardHeight", 0.0);
    play(rig, 64);
    CHECK(start->isVisible());
}

TEST_CASE("details.a11y: on the phone's window the names, targets and large text hold, and the page scrolls",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig;
    load_screen(rig, world);
    QQuickItem* scroller = need(rig, QStringLiteral("headerScroller"));
    REQUIRE(scroller != nullptr);

    expect_clean(check_names(rig));
    expect_clean(check_targets(rig));
    expect_clean(check_text_scale(rig));
    // The content is longer than the window, so it scrolls; the foot bar stays.
    CHECK(scroller->property("contentHeight").toDouble() > scroller->height());
    CHECK(need(rig, QStringLiteral("headerStart"))->isVisible());

    rig.set_text_scale(3.1);
    rig.layout();
    expect_clean(check_text_scale(rig));
    const QRectF area = rig.geometry(scroller);
    const QRectF start = rig.geometry(need(rig, QStringLiteral("headerStart")));
    CHECK(area.height() > 0);
    CHECK(start.top() >= area.bottom() - 0.5);
    CHECK(start.bottom() <= rig.window().height() + 0.5);
}

TEST_CASE("details.a11y: names, targets, order and text scale, in light and dark", "[ui][header_screen]")
{
    // A window tall enough to show the whole screen: the checks compare positions in the window and know nothing of a
    // scroller, so the foot bar must not sit above rows that are scrolled below it.
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    QQuickItem* screen = load_screen(rig, world);
    REQUIRE(screen != nullptr);

    SECTION("light")
    {
        expect_clean(check_everything(rig));
    }

    SECTION("dark")
    {
        rig.set_dark(true);
        rig.settle();
        expect_clean(check_everything(rig));
    }

    SECTION("with the Other field open")
    {
        rig.tap(need(rig, QStringLiteral("headerOther")));
        rig.settle();
        rig.layout();
        expect_clean(check_everything(rig));
    }

    SECTION("the safe area, at the design's insets and at larger ones")
    {
        for (const auto& [top, bottom] : {std::pair{59.0, 34.0}, std::pair{79.0, 54.0}})
        {
            screen->setProperty("overrideInsets", true);
            screen->setProperty("overrideTop", top);
            screen->setProperty("overrideBottom", bottom);
            rig.layout();
            expect_clean(check_safe_area(rig, top, bottom));
        }
    }

    SECTION("with Reduce Motion nothing moves for long")
    {
        rig.set_reduce_motion(true);
        expect_clean(check_motion_reduced(rig));
    }
}
