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
#include <QColor>
#include <QFont>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

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

/// A colour of `Theme.colour` in the scheme the rig has now.
QColor theme_colour(UiRig& rig, const char* name)
{
    QObject* colours = rig.theme()->property("colour").value<QObject*>();
    return colours != nullptr ? colours->property(name).value<QColor>() : QColor();
}

/// The accessible name of an item, or text for a `Text`.
QString name_of_item(UiRig& rig, QQuickItem* item)
{
    for (const Node& node : rig.tree())
    {
        if (node.m_Item == item)
        {
            return node.m_Name;
        }
    }
    return QString();
}

QString input_text(UiRig& rig, const char* field)
{
    QQuickItem* input = input_of(rig, need(rig, QString::fromLatin1(field)));
    return input != nullptr ? input->property("text").toString() : QString();
}

/// Stands for the ShareService: whether the platform has a share sheet, and how many times "Share this log first"
/// asked.
class FakeShare : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)

public:
    explicit FakeShare(bool available) : m_Available(available) {}

    [[nodiscard]] bool available() const noexcept
    {
        return m_Available;
    }

    Q_INVOKABLE bool shareOpenLog()
    {
        ++m_Shares;
        return true;
    }

    int m_Shares = 0;

private:
    bool m_Available;
};

QQuickItem* load_screen(UiRig& rig, HeaderWorld& world, FakeShare* share = nullptr)
{
    world.attach(rig);
    QVariantMap models = world.models();
    if (share != nullptr)
    {
        models.insert(QStringLiteral("share"), QVariant::fromValue(static_cast<QObject*>(share)));
    }
    QQuickItem* screen = rig.load(QStringLiteral("HeaderScreen"), models);
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
        CHECK((input->property("inputMethodHints").toInt() & static_cast<int>(Qt::ImhDigitsOnly)) != 0);
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
        // The design draws no check mark on the unit number, only on the version.
        CHECK_FALSE(unit->property("valid").toBool());
        CHECK(world.timeline().entry_count() == 0);
    }
}

TEST_CASE("details.unit_keypad: the unit number and the version are set in the 20 pt face of the design",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);

    for (const char* name : {"headerUnit", "headerVersion"})
    {
        const QFont font = input_of(rig, need(rig, QString::fromLatin1(name)))->property("font").value<QFont>();
        CHECK(font.pixelSize() == 20);
        CHECK(font.weight() == QFont::DemiBold);
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

TEST_CASE("details.version_change: the Software version sheet opens from the check mark of the version field",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* version = need(rig, QStringLiteral("headerVersion"));
    QQuickItem* sheet = need(rig, QStringLiteral("versionSheet"));

    // The design has no row for it: the screen has none, and the way to the sheet is the check mark.
    CHECK(rig.find(QStringLiteral("headerChangeVersion")) == nullptr);
    CHECK(version->property("accessoryName").toString() == QStringLiteral("The version changed during the day"));
    QQuickItem* accessory = need(rig, QStringLiteral("formAccessory"), version);
    CHECK(accessory->isVisible());
    CHECK(accessory->width() >= 44.0);
    CHECK(accessory->height() >= 44.0);
    CHECK(name_of_item(rig, accessory) == QStringLiteral("The version changed during the day"));

    CHECK_FALSE(sheet->property("isOpen").toBool());
    rig.tap(accessory);
    play(rig, 500);
    CHECK(sheet->property("isOpen").toBool());
    CHECK(sheet->property("fromText").toString() == QStringLiteral("1.4.2"));

    SECTION("a version being typed has no way to the sheet until it is the one in force")
    {
        rig.tap(need(rig, QStringLiteral("navLeading"), sheet));
        play(rig, 500);
        QQuickItem* input = input_of(rig, version);
        rig.tap(input);
        rig.type(QStringLiteral("0"));
        rig.settle();
        CHECK(version->property("accessoryName").toString().isEmpty());
        CHECK_FALSE(need(rig, QStringLiteral("formAccessory"), version)->isVisible());
    }
}

TEST_CASE("details.version_change: with no version yet there is nothing to change", "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* version = need(rig, QStringLiteral("headerVersion"));
    CHECK(version->property("accessoryName").toString().isEmpty());
    CHECK_FALSE(need(rig, QStringLiteral("formAccessory"), version)->isVisible());

    // A first fill is a Correct: the check mark appears with the value and is a button once the value is in force.
    retype(rig, version, QStringLiteral("1.4.2"));
    CHECK(version->property("accessoryName").toString() == QStringLiteral("The version changed during the day"));
    CHECK(need(rig, QStringLiteral("formAccessory"), version)->isVisible());
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
        rig.tap(need(rig, QStringLiteral("navLeading"), need(rig, QStringLiteral("headerNav"))));
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
    rig.tap(need(rig, QStringLiteral("navLeading"), need(rig, QStringLiteral("headerNav"))));
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
        // The log has entries and has never been shared, and the alert says so.
        CHECK(alert->property("message").toString() ==
              QStringLiteral("UNIT-27 at North Site has 2 entries and hasn’t been shared. It stays in History. The new "
                             "log starts empty, with Sam Example as facilitator."));
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

TEST_CASE("details.new_log_row: the row is the design's, with the plus, in the link colour", "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerNewLogRow"));
    REQUIRE(row != nullptr);

    CHECK(row->property("leadingIcon").toString() == QStringLiteral("plus"));
    CHECK(row->property("labelColour").value<QColor>() == theme_colour(rig, "primaryText"));
    CHECK(row->height() >= 56.0);
    CHECK(row->property("showChevron").toBool());
    QQuickItem* group = need(rig, QStringLiteral("headerNewLog"));
    CHECK(group->property("title").toString() == QStringLiteral("Another unit or site"));
    CHECK(group->property("footer").toString() ==
          QStringLiteral("Keeps today’s log in History and opens an empty one. Only the facilitator carries over."));
}

TEST_CASE("details.new_log_row: an unshared log offers to be shared first, and the alert comes back",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    world.record_errors(2);
    REQUIRE(world.session().unshared());
    FakeShare share(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world, &share);
    QQuickItem* alert = need(rig, QStringLiteral("newLogAlert"));

    rig.tap(need(rig, QStringLiteral("headerNewLogRow")));
    play(rig, 400);
    REQUIRE(alert->property("isOpen").toBool());
    QQuickItem* alternate = need(rig, QStringLiteral("alertAlternate"), alert);
    CHECK(alternate->isVisible());
    CHECK(alternate->property("label").toString() == QStringLiteral("Share this log first"));

    rig.tap(alternate);
    play(rig, 800);
    rig.settle();
    CHECK(share.m_Shares == 1);
    // The log is still open and unchanged, and the alert is up again for the person to go on.
    CHECK(world.session().live_entry_count() == 2);
    CHECK(world.storage().repository().call_count(RepositoryOp::START_NEW_LOG) == 0);
    CHECK(alert->property("isOpen").toBool());
}

TEST_CASE("details.new_log_row: without a share sheet, or with the log shared, there is no \"Share this log first\"",
          "[ui][header_screen]")
{
    SECTION("the platform has no share sheet")
    {
        HeaderWorld world(true);
        world.record_errors(1);
        FakeShare share(false);
        UiRig rig(TALL_WINDOW);
        load_screen(rig, world, &share);
        rig.tap(need(rig, QStringLiteral("headerNewLogRow")));
        play(rig, 400);
        CHECK_FALSE(need(rig, QStringLiteral("alertAlternate"))->isVisible());
    }

    SECTION("a log with no entries has not been left unshared")
    {
        HeaderWorld world(true);
        FakeShare share(true);
        UiRig rig(TALL_WINDOW);
        load_screen(rig, world, &share);
        REQUIRE_FALSE(world.session().unshared());
        rig.tap(need(rig, QStringLiteral("headerNewLogRow")));
        play(rig, 400);
        CHECK_FALSE(need(rig, QStringLiteral("alertAlternate"))->isVisible());
        CHECK(need(rig, QStringLiteral("newLogAlert"))->property("message").toString() ==
              QStringLiteral("UNIT-27 at North Site has 0 entries. It stays in History. The new log starts empty, with "
                             "Sam Example as facilitator."));
    }
}

TEST_CASE("details.title: the large title collapses to the compact bar once the page has scrolled",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig;
    load_screen(rig, world);
    QQuickItem* scroller = need(rig, QStringLiteral("headerScroller"));
    QQuickItem* nav = need(rig, QStringLiteral("headerNav"));
    QQuickItem* large = need(rig, QStringLiteral("navTitleBelow"), nav);
    QQuickItem* compact = need(rig, QStringLiteral("navTitle"), nav);
    REQUIRE(scroller != nullptr);
    REQUIRE(large != nullptr);
    REQUIRE(compact != nullptr);

    // At rest the page starts under the large title, and nothing has scrolled.
    CHECK(large->isVisible());
    CHECK_FALSE(compact->isVisible());
    CHECK(large->property("text").toString() == QStringLiteral("Today’s details"));
    CHECK(scroller->property("contentY").toDouble() == -scroller->property("topMargin").toDouble());
    CHECK(scroller->property("topMargin").toDouble() > 0.0);

    // Scrolled: the compact bar with the centred title and the divider, as HeaderNewLogRow has it.
    scroller->setProperty("contentY", -scroller->property("topMargin").toDouble() + 40.0);
    play(rig, 400);
    CHECK_FALSE(large->isVisible());
    CHECK(compact->isVisible());
    // The compact title fades in over 240 ms and ends in the full ink of the design (HeaderNewLogRow), not part way.
    CHECK(compact->opacity() == 1.0);
    CHECK(compact->property("color").value<QColor>() == theme_colour(rig, "textPrimary"));
    CHECK(compact->property("text").toString() == QStringLiteral("Today’s details"));
    const QRectF title = rig.geometry(compact);
    CHECK(std::abs(title.center().x() - rig.window().width() / 2.0) < 1.0);

    // Back at the top it is large again.
    scroller->setProperty("contentY", -scroller->property("topMargin").toDouble());
    play(rig, 400);
    CHECK(large->isVisible());
    CHECK_FALSE(compact->isVisible());
}

TEST_CASE("details.footer: the button and its line end at the bottom of the safe area, 10 pt apart",
          "[ui][header_screen]")
{
    HeaderWorld world;
    UiRig rig;
    QQuickItem* screen = load_screen(rig, world);
    // The window of the rig has no safe area; the design's is 59 pt above and 34 pt below.
    screen->setProperty("overrideInsets", true);
    screen->setProperty("overrideTop", 59.0);
    screen->setProperty("overrideBottom", 34.0);
    rig.layout();
    const QRectF start = rig.geometry(need(rig, QStringLiteral("headerStart")));
    QQuickItem* note = text_item(rig, QStringLiteral("You can log errors before every field is filled."));
    REQUIRE(note != nullptr);
    const QRectF line = rig.geometry(note);
    CHECK(start.height() >= 54.0);
    CHECK(line.top() - start.bottom() == 10.0);
    CHECK(line.bottom() == 874.0 - 34.0);
}

TEST_CASE("details.footer: once the page has scrolled the line goes and the button sits at the bottom of the safe area",
          "[ui][header_screen]")
{
    HeaderWorld world(true);
    UiRig rig;
    QQuickItem* screen = load_screen(rig, world);
    screen->setProperty("overrideInsets", true);
    screen->setProperty("overrideTop", 59.0);
    screen->setProperty("overrideBottom", 34.0);
    rig.layout();
    QQuickItem* scroller = need(rig, QStringLiteral("headerScroller"));
    QQuickItem* start = need(rig, QStringLiteral("headerStart"));
    QQuickItem* note = text_item(rig, QStringLiteral("You can log errors before every field is filled."));
    REQUIRE(note != nullptr);

    // At rest, the line is under the button.
    CHECK(note->isVisible());
    const double page_bottom = rig.geometry(scroller).bottom();

    scroller->setProperty("contentY", -scroller->property("topMargin").toDouble() + 40.0);
    play(rig, 400);
    CHECK_FALSE(note->isVisible());
    CHECK(rig.geometry(start).bottom() == 874.0 - 34.0);
    // The page above the foot did not change size, so that it does not move under the finger.
    CHECK(rig.geometry(scroller).bottom() == page_bottom);

    scroller->setProperty("contentY", -scroller->property("topMargin").toDouble());
    play(rig, 400);
    CHECK(note->isVisible());
    CHECK(rig.geometry(note).bottom() == 874.0 - 34.0);
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

    // The version field is under the foot bar while the page is at rest (the Environment row is above it): the person
    // scrolls to it first.
    scroller->setProperty("contentY", scroller->property("contentHeight").toDouble() - scroller->height());
    play(rig, 64);
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
    // The content is longer than the window, so it scrolls; the foot bar stays. The page starts under the bar, so the
    // room it needs is its height and that margin.
    CHECK(scroller->property("contentHeight").toDouble() + scroller->property("topMargin").toDouble() >
          scroller->height());
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

// ------------------------------------------------------------------------------------------------------ environment

TEST_CASE("details.environment: a row under the unit field says None until one is picked",
          "[ui][header_screen][environment]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerEnvironment"));
    QQuickItem* unit = need(rig, QStringLiteral("headerUnit"));

    CHECK(row->property("text").toString() == QStringLiteral("Environment"));
    CHECK(row->property("value").toString() == QStringLiteral("None"));
    CHECK(row->property("showChevron").toBool());
    CHECK(row->height() >= 44.0);
    // It sits under the unit number and above the location, as the design has it.
    CHECK(rig.geometry(row).top() >= rig.geometry(unit).bottom());
    CHECK(rig.geometry(row).bottom() <= rig.geometry(need(rig, QStringLiteral("headerLocations"))).top());
    // The unit field is unchanged: the prefix, the digits.
    CHECK(unit->property("prefixText").toString() == QStringLiteral("UNIT-"));
    CHECK(input_text(rig, "headerUnit") == QStringLiteral("27"));
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
    // The sheet is not open.
    CHECK_FALSE(need(rig, QStringLiteral("environmentSheet"))->property("isOpen").toBool());
}

TEST_CASE("details.environment: choosing prod saves it with the unit, and choosing None takes it away",
          "[ui][header_screen][environment]")
{
    HeaderWorld world(true);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerEnvironment"));
    QQuickItem* sheet = need(rig, QStringLiteral("environmentSheet"));
    const auto writes = [&world] { return world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER); };

    open_environment_sheet(rig);
    rig.tap(choice(rig, "environment_prod"));
    play(rig, 500);
    rig.settle();
    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27-prod"));
    CHECK(world.session().unit_environment() == QStringLiteral("prod"));
    CHECK(row->property("value").toString() == QStringLiteral("prod"));
    CHECK(writes() == 1);
    CHECK(world.timeline().entry_count() == 0); // a Correct: no entry
    // The digits in the field are untouched, and the field does not show the environment.
    CHECK(input_text(rig, "headerUnit") == QStringLiteral("27"));

    // Open again: prod is ticked, None is not. dev replaces prod.
    open_environment_sheet(rig);
    CHECK(choice(rig, "environment_prod")->property("checked").toBool());
    CHECK_FALSE(choice(rig, "environmentNone")->property("checked").toBool());
    rig.tap(choice(rig, "environment_dev"));
    play(rig, 500);
    rig.settle();
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27-dev"));
    CHECK(row->property("value").toString() == QStringLiteral("dev"));

    // None takes it away.
    open_environment_sheet(rig);
    rig.tap(choice(rig, "environmentNone"));
    play(rig, 500);
    rig.settle();
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27"));
    CHECK(world.session().unit_environment().isEmpty());
    CHECK(row->property("value").toString() == QStringLiteral("None"));
    CHECK(writes() == 3);

    // Choosing what is already chosen writes nothing.
    open_environment_sheet(rig);
    rig.tap(choice(rig, "environmentNone"));
    play(rig, 500);
    rig.settle();
    CHECK(writes() == 3);
}

TEST_CASE("details.environment: a typed number and the environment are saved together",
          "[ui][header_screen][environment]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerEnvironment"));
    QQuickItem* unit = need(rig, QStringLiteral("headerUnit"));
    const auto writes = [&world] { return world.storage().repository().call_count(RepositoryOp::CORRECT_HEADER); };

    SECTION("the environment is picked first and waits for the number")
    {
        open_environment_sheet(rig);
        rig.tap(choice(rig, "environment_prod"));
        play(rig, 500);
        rig.settle();
        CHECK(row->property("value").toString() == QStringLiteral("prod"));
        CHECK(world.session().unit_text().isEmpty());
        CHECK(writes() == 0);
        // The note under the row says so.
        CHECK(text_item(rig, QStringLiteral("Saved with the number, once you have typed it.")) != nullptr);

        retype(rig, unit, QStringLiteral("27"));
        rig.settle();
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-27-prod"));
        CHECK(writes() == 1);
        CHECK(row->property("value").toString() == QStringLiteral("prod"));
        CHECK(text_item(rig, QStringLiteral("Saved with the number, once you have typed it.")) == nullptr);
    }

    SECTION("the number is typed first and the environment saved with it when picked")
    {
        rig.tap(input_of(rig, unit));
        rig.type(QStringLiteral("31"));
        open_environment_sheet(rig);
        rig.tap(choice(rig, "environment_dev"));
        play(rig, 500);
        rig.settle();
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-31-dev"));
        // Leaving the number field to open the row saved the number; picking the environment saved it with the number.
        CHECK(writes() == 2);
    }

    SECTION("a refused number is not saved with the environment")
    {
        retype(rig, unit, QStringLiteral("027"));
        open_environment_sheet(rig);
        rig.tap(choice(rig, "environment_prod"));
        play(rig, 500);
        rig.settle();
        CHECK(world.session().unit_text().isEmpty());
        CHECK(writes() == 0);
        CHECK_FALSE(unit->property("errorText").toString().isEmpty());
    }

    SECTION("Start logging saves what is valid, the environment with the number")
    {
        open_environment_sheet(rig);
        rig.tap(choice(rig, "environment_prod"));
        play(rig, 500);
        rig.tap(input_of(rig, unit));
        rig.type(QStringLiteral("12"));
        rig.tap(need(rig, QStringLiteral("headerStart")));
        rig.settle();
        CHECK(world.session().unit_text() == QStringLiteral("UNIT-12-prod"));
    }
}

TEST_CASE("details.environment: the digits field never takes the stored form", "[ui][header_screen][environment]")
{
    HeaderWorld world;
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* unit = need(rig, QStringLiteral("headerUnit"));

    retype(rig, unit, QStringLiteral("27-prod"));
    CHECK_FALSE(unit->property("errorText").toString().isEmpty());
    CHECK(world.session().unit_text().isEmpty());
}

TEST_CASE("details.environment: the choices are the ones of the setup file, in its order",
          "[ui][header_screen][environment]")
{
    ej::Configuration configuration = ej::test::sample_configuration();
    configuration.m_UnitEnvironments = {"staging", "prod", "pre-prod"};
    HeaderWorld world(true, configuration);
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    open_environment_sheet(rig);
    CHECK(sheet_choices(rig) == QStringList{"", "staging", "prod", "pre-prod"});

    rig.tap(choice(rig, "environment_pre-prod"));
    play(rig, 500);
    rig.settle();
    CHECK(world.session().unit_text() == QStringLiteral("UNIT-27-pre-prod"));
}

TEST_CASE("details.environment: a new log starts with None", "[ui][header_screen][environment]")
{
    HeaderWorld world(true);
    REQUIRE(world.session().correctUnit(QStringLiteral("27"), QStringLiteral("prod")));
    world.gateway().run_all();
    UiRig rig(TALL_WINDOW);
    load_screen(rig, world);
    QQuickItem* row = need(rig, QStringLiteral("headerEnvironment"));
    CHECK(row->property("value").toString() == QStringLiteral("prod"));

    REQUIRE(world.session().startNewLog());
    rig.settle();
    CHECK(row->property("value").toString() == QStringLiteral("None"));
    CHECK(input_text(rig, "headerUnit").isEmpty());
}

#include "header_screen_test.moc"
