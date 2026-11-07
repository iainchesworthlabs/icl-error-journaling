// SPDX-License-Identifier: GPL-3.0-or-later
// TimeTumbler (docs/plan/stage-4.md, 4.6, decision 11): time_tumbler.values, and the error text, the accessibility and
// the touch of the columns.
#include "input_components_support.hpp"
#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QQuickItem>
#include <QSignalSpy>

using namespace ej::ui;
using namespace ej::ui::input;

namespace
{

constexpr int HOUR = 3600;
constexpr int MINUTE = 60;

// The host plays the part of the Edit entry sheet: it holds the time and stores what the tumbler says was chosen. The
// conversion to an instant, and the refusal of the hour the clocks skip, are the view model's and not the tumbler's.
constexpr auto SHEET = R"(
    property int seconds: 11 * 3600 + 2 * 60
    property bool twelve: false
    property bool withSeconds: false
    property string refusal: ""
    TimeTumbler {
        id: tumbler
        objectName: "tumbler"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        localSeconds: host.seconds
        twelveHour: host.twelve
        showSeconds: host.withSeconds
        errorText: host.refusal
        onTimeEdited: value => host.seconds = value
    })";

struct Fixture
{
    Fixture()
    {
        REQUIRE(load_host(rig, QString::fromUtf8(SHEET)) != nullptr);
        INFO(rig.errors().toStdString());
        tumbler = rig.find(QStringLiteral("tumbler"));
        REQUIRE(tumbler != nullptr);
        hours = rig.find(QStringLiteral("Hours"));
        minutes = rig.find(QStringLiteral("Minutes"));
        secs = rig.find(QStringLiteral("Seconds"));
        meridiem = rig.find(QStringLiteral("AM/PM"));
        REQUIRE(hours != nullptr);
        REQUIRE(minutes != nullptr);
        REQUIRE(secs != nullptr);
        REQUIRE(meridiem != nullptr);
    }

    void set(const char* name, const QVariant& value)
    {
        rig.root()->setProperty(name, value);
        rig.layout();
    }

    [[nodiscard]] int seconds() const
    {
        return rig.root()->property("seconds").toInt();
    }

    static int index(QQuickItem* column)
    {
        return column->property("currentIndex").toInt();
    }

    static QStringList labels(QQuickItem* column)
    {
        return column->property("model").toStringList();
    }

    /// VoiceOver's swipe up (+1) or down (-1) on a column.
    static void adjust(QQuickItem* column, int delta)
    {
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(column);
        REQUIRE(iface != nullptr);
        QAccessibleActionInterface* actions = iface->actionInterface();
        REQUIRE(actions != nullptr);
        actions->doAction(delta > 0 ? QAccessibleActionInterface::increaseAction()
                                    : QAccessibleActionInterface::decreaseAction());
    }

    UiRig rig;
    QQuickItem* tumbler = nullptr;
    QQuickItem* hours = nullptr;
    QQuickItem* minutes = nullptr;
    QQuickItem* secs = nullptr;
    QQuickItem* meridiem = nullptr;
};

} // namespace

TEST_CASE("time_tumbler.values: hours and minutes of the 24-hour format show the time, with no seconds or AM/PM",
          "[ui][time_tumbler]")
{
    Fixture f;
    CHECK(f.index(f.hours) == 11);
    CHECK(f.index(f.minutes) == 2);
    CHECK(f.hours->isVisible());
    CHECK(f.minutes->isVisible());
    CHECK_FALSE(f.secs->isVisible());
    CHECK_FALSE(f.meridiem->isVisible());

    const QStringList hours = f.labels(f.hours);
    REQUIRE(hours.size() == 24);
    CHECK(hours.front() == QStringLiteral("00"));
    CHECK(hours.at(9) == QStringLiteral("09"));
    CHECK(hours.back() == QStringLiteral("23"));
    const QStringList minutes = f.labels(f.minutes);
    REQUIRE(minutes.size() == 60);
    CHECK(minutes.at(2) == QStringLiteral("02"));
    CHECK(minutes.back() == QStringLiteral("59"));
}

TEST_CASE("time_tumbler.values: a change emits timeEdited once, with local seconds, and moves by one",
          "[ui][time_tumbler]")
{
    Fixture f;
    QSignalSpy edited(f.tumbler, SIGNAL(timeEdited(int)));

    f.adjust(f.hours, +1);
    REQUIRE(edited.count() == 1);
    CHECK(edited.at(0).at(0).toInt() == 12 * HOUR + 2 * MINUTE);
    CHECK(f.index(f.hours) == 12);
    CHECK(f.seconds() == 12 * HOUR + 2 * MINUTE);

    f.adjust(f.minutes, +1);
    REQUIRE(edited.count() == 2);
    CHECK(edited.at(1).at(0).toInt() == 12 * HOUR + 3 * MINUTE);

    f.adjust(f.minutes, -1);
    f.adjust(f.minutes, -1);
    REQUIRE(edited.count() == 4);
    CHECK(edited.at(3).at(0).toInt() == 12 * HOUR + 1 * MINUTE);

    // The tumbler never assigns localSeconds itself: the screen's value is what the screen stored.
    CHECK(f.tumbler->property("localSeconds").toInt() == f.seconds());
}

TEST_CASE("time_tumbler.values: the columns wrap and do not carry into each other", "[ui][time_tumbler]")
{
    Fixture f;
    QSignalSpy edited(f.tumbler, SIGNAL(timeEdited(int)));

    f.set("seconds", 0);
    CHECK(f.index(f.hours) == 0);
    CHECK(f.index(f.minutes) == 0);
    CHECK(edited.count() == 0);

    f.adjust(f.hours, -1);
    CHECK(f.seconds() == 23 * HOUR);
    f.adjust(f.hours, +1);
    CHECK(f.seconds() == 0);
    f.adjust(f.minutes, -1);
    CHECK(f.seconds() == 59 * MINUTE);
    f.adjust(f.minutes, +1);
    CHECK(f.seconds() == 0);
    CHECK(edited.count() == 4);
}

TEST_CASE("time_tumbler.values: a time set from outside moves the columns and is not reported as edited",
          "[ui][time_tumbler]")
{
    Fixture f;
    QSignalSpy edited(f.tumbler, SIGNAL(timeEdited(int)));

    f.set("seconds", 20 * HOUR + 45 * MINUTE);
    CHECK(f.index(f.hours) == 20);
    CHECK(f.index(f.minutes) == 45);
    f.set("seconds", 86399);
    CHECK(f.index(f.hours) == 23);
    CHECK(f.index(f.minutes) == 59);
    CHECK(edited.count() == 0);
}

TEST_CASE("time_tumbler.values: the 12-hour format has an AM/PM column, 12 for noon and midnight, and keeps the time",
          "[ui][time_tumbler]")
{
    Fixture f;
    QSignalSpy edited(f.tumbler, SIGNAL(timeEdited(int)));

    f.set("seconds", 15 * HOUR + 30 * MINUTE);
    f.set("twelve", true);
    CHECK(f.meridiem->isVisible());
    CHECK(f.labels(f.hours) == QStringList{"12", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11"});
    CHECK(f.index(f.hours) == 3);
    CHECK(f.index(f.minutes) == 30);
    CHECK(f.index(f.meridiem) == 1);
    CHECK(f.labels(f.meridiem) == QStringList{"AM", "PM"});
    // Changing the format is not an edit, and the time is the same.
    CHECK(edited.count() == 0);
    CHECK(f.seconds() == 15 * HOUR + 30 * MINUTE);

    f.set("seconds", 0);
    CHECK(f.index(f.hours) == 0);
    CHECK(f.index(f.meridiem) == 0);
    f.set("seconds", 12 * HOUR + 30 * MINUTE);
    CHECK(f.index(f.hours) == 0);
    CHECK(f.index(f.meridiem) == 1);
    f.set("seconds", 23 * HOUR + 2 * MINUTE);
    CHECK(f.index(f.hours) == 11);
    CHECK(f.index(f.meridiem) == 1);

    // PM to AM is twelve hours earlier, and AM does not go below AM.
    f.adjust(f.meridiem, -1);
    REQUIRE(edited.count() == 1);
    CHECK(edited.at(0).at(0).toInt() == 11 * HOUR + 2 * MINUTE);
    f.adjust(f.meridiem, -1);
    CHECK(edited.count() == 1);
    f.adjust(f.meridiem, +1);
    CHECK(f.seconds() == 23 * HOUR + 2 * MINUTE);

    // The hour 12 moves to 1 in the same half of the day.
    f.set("seconds", 12 * HOUR);
    f.adjust(f.hours, +1);
    CHECK(f.seconds() == 13 * HOUR);

    f.set("twelve", false);
    CHECK_FALSE(f.meridiem->isVisible());
    CHECK(f.index(f.hours) == 13);
    CHECK(f.labels(f.hours).size() == 24);
}

TEST_CASE("time_tumbler.values: the seconds column shows when the time format has seconds, and hidden seconds are kept",
          "[ui][time_tumbler]")
{
    Fixture f;
    QSignalSpy edited(f.tumbler, SIGNAL(timeEdited(int)));

    f.set("seconds", 11 * HOUR + 2 * MINUTE + 30);
    // Hidden: an edit of the minutes keeps the 30 seconds.
    f.adjust(f.minutes, +1);
    CHECK(f.seconds() == 11 * HOUR + 3 * MINUTE + 30);

    f.set("withSeconds", true);
    CHECK(f.secs->isVisible());
    CHECK(f.index(f.secs) == 30);
    CHECK(f.labels(f.secs).size() == 60);
    f.adjust(f.secs, +1);
    CHECK(f.seconds() == 11 * HOUR + 3 * MINUTE + 31);
    f.adjust(f.secs, -1);
    f.adjust(f.secs, -1);
    CHECK(f.seconds() == 11 * HOUR + 3 * MINUTE + 29);
    CHECK(edited.count() == 4);

    // The 12-hour format with seconds has four columns, and they fit in the card.
    f.set("twelve", true);
    const QRectF card = f.rig.geometry(f.tumbler);
    for (QQuickItem* column : {f.hours, f.minutes, f.secs, f.meridiem})
    {
        CHECK(column->isVisible());
        CHECK(f.rig.geometry(column).left() >= card.left());
        CHECK(f.rig.geometry(column).right() <= card.right());
        CHECK(f.rig.geometry(column).width() >= 44);
    }
    CHECK(f.rig.geometry(f.hours).right() <= f.rig.geometry(f.minutes).left());
    CHECK(f.rig.geometry(f.minutes).right() <= f.rig.geometry(f.secs).left());
    CHECK(f.rig.geometry(f.secs).right() <= f.rig.geometry(f.meridiem).left());
}

TEST_CASE("time_tumbler.error: the refusal of the model is shown under the columns and read with the group",
          "[ui][time_tumbler]")
{
    Fixture f;
    const double height = f.rig.geometry(f.tumbler).height();
    const QString message =
        QStringLiteral("That time does not exist on that day, because the clocks go forward. Choose another time.");
    QAccessibleInterface* group = QAccessible::queryAccessibleInterface(f.tumbler);
    REQUIRE(group != nullptr);
    CHECK(group->text(QAccessible::Description).isEmpty());
    CHECK_FALSE(f.tumbler->property("invalid").toBool());

    f.set("refusal", message);
    QQuickItem* text = nullptr;
    for (const Node& node : f.rig.tree(f.tumbler))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) && node.m_Visible &&
            node.m_Item->property("text").toString() == message)
        {
            text = node.m_Item;
        }
    }
    REQUIRE(text != nullptr);
    CHECK(text->property("color").value<QColor>() == theme_colour(f.rig, "destructive"));
    CHECK(f.rig.geometry(text).top() >= f.rig.geometry(f.hours).bottom());
    CHECK(f.rig.geometry(f.tumbler).height() > height);
    CHECK(group->text(QAccessible::Description) == message);
    CHECK(f.tumbler->property("invalid").toBool());
    CHECK(f.rig.geometry(text).right() <= 402);
    expect_clean(check_text_scale(f.rig));

    f.set("refusal", QString());
    CHECK(f.rig.geometry(f.tumbler).height() == height);
    CHECK_FALSE(f.tumbler->property("invalid").toBool());
}

TEST_CASE("time_tumbler.voiceover: each column is an adjustable item that reads its name and value",
          "[ui][time_tumbler]")
{
    Fixture f;
    QAccessibleInterface* group = QAccessible::queryAccessibleInterface(f.tumbler);
    REQUIRE(group != nullptr);
    CHECK(group->role() == QAccessible::Grouping);
    CHECK(group->text(QAccessible::Name) == QStringLiteral("Time"));

    struct Expected
    {
        QQuickItem* column;
        const char* name;
        const char* value;
    };
    for (const Expected& expected : {Expected{f.hours, "Hours", "11"}, Expected{f.minutes, "Minutes", "02"}})
    {
        QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(expected.column);
        REQUIRE(iface != nullptr);
        CHECK(iface->role() == QAccessible::SpinBox);
        CHECK(iface->text(QAccessible::Name) == QString::fromLatin1(expected.name));
        CHECK(iface->text(QAccessible::Description) == QString::fromLatin1(expected.value));
        REQUIRE(iface->actionInterface() != nullptr);
        const QStringList actions = iface->actionInterface()->actionNames();
        CHECK(actions.contains(QAccessibleActionInterface::increaseAction()));
        CHECK(actions.contains(QAccessibleActionInterface::decreaseAction()));
    }

    // After an adjustment the value read out is the new one.
    f.adjust(f.hours, +1);
    CHECK(QAccessible::queryAccessibleInterface(f.hours)->text(QAccessible::Description) == QStringLiteral("12"));

    // They are read in the order they appear.
    QStringList order;
    for (const Node& node : f.rig.accessible_tree(f.tumbler))
    {
        if (node.m_Role == QAccessible::SpinBox && node.m_Visible)
        {
            order.push_back(node.m_Name);
        }
    }
    CHECK(order == QStringList{QStringLiteral("Hours"), QStringLiteral("Minutes")});
}

TEST_CASE("time_tumbler.touch: a drag on a column changes it, and each change is reported once", "[ui][time_tumbler]")
{
    Fixture f;
    QSignalSpy edited(f.tumbler, SIGNAL(timeEdited(int)));
    const int before = f.index(f.minutes);

    const double row = f.rig.geometry(f.minutes).height() / 6;
    // Up by three rows: the minutes go up by three (the column is dragged up to bring later values to the middle).
    f.rig.drag(f.minutes, QPointF(40, row * 3), QPointF(40, 0), 8);
    play(f.rig, 800);
    const int after = f.index(f.minutes);
    INFO("minutes " << before << " to " << after);
    CHECK(after != before);
    CHECK(edited.count() >= 1);
    // Every report is a different time from the one before it, and the last is what the columns show.
    int last = 11 * HOUR + 2 * MINUTE;
    for (const QList<QVariant>& arguments : edited)
    {
        CHECK(arguments.at(0).toInt() != last);
        last = arguments.at(0).toInt();
    }
    CHECK(last == f.seconds());
    CHECK(last == 11 * HOUR + after * MINUTE);
}

TEST_CASE("time_tumbler.values: the columns are in place at once, with the value in the band and no spin", "[ui][time_tumbler]")
{
    Fixture f;
    // No time has been played: the selected rows are already level with the band.
    QQuickItem* band = nullptr;
    for (const Node& node : f.rig.tree(f.tumbler))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) &&
            node.m_Item->property("color").value<QColor>() == theme_colour(f.rig, "timeChipBackground"))
        {
            band = node.m_Item;
        }
    }
    REQUIRE(band != nullptr);
    const double middle = f.rig.geometry(band).center().y();

    const auto level = [&f, middle](QQuickItem* column, const QString& text)
    {
        bool found = false;
        for (const Node& node : f.rig.tree(column))
        {
            if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) &&
                node.m_Item->property("text").toString() == text && node.m_Rect.center().y() > f.rig.geometry(column).top())
            {
                found = true;
                CHECK(std::abs(node.m_Rect.center().y() - middle) < 1.0);
            }
        }
        CHECK(found);
    };
    level(f.hours, QStringLiteral("11"));
    level(f.minutes, QStringLiteral("02"));

    // A time from outside is also shown at once.
    f.set("seconds", 20 * HOUR + 45 * MINUTE);
    level(f.hours, QStringLiteral("20"));
    level(f.minutes, QStringLiteral("45"));
}

TEST_CASE("time_tumbler.geometry: five rows of 44 pt, the selected row banded, and taller rows with larger text",
          "[ui][time_tumbler]")
{
    Fixture f;
    CHECK(f.rig.geometry(f.hours).height() == 5 * 44);
    CHECK(f.rig.geometry(f.hours).width() == 88);
    CHECK(f.rig.geometry(f.hours).top() == f.rig.geometry(f.minutes).top());

    // The band behind the middle row is the time chip colour, one row high.
    QQuickItem* band = nullptr;
    for (const Node& node : f.rig.tree(f.tumbler))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) &&
            node.m_Item->property("color").value<QColor>() == theme_colour(f.rig, "timeChipBackground"))
        {
            band = node.m_Item;
        }
    }
    REQUIRE(band != nullptr);
    CHECK(f.rig.geometry(band).height() == 44);
    CHECK(f.rig.geometry(band).top() == f.rig.geometry(f.hours).top() + 2 * 44);

    f.rig.set_text_scale(3.1);
    CHECK(f.rig.geometry(f.hours).height() > 5 * 44);
}

TEST_CASE("time_tumbler.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][time_tumbler]")
{
    Fixture f;
    expect_clean(check_everything(f.rig));
    f.rig.set_dark(true);
    expect_clean(check_everything(f.rig));
    f.rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(f.rig));

    // The 12-hour format with seconds, with a refusal showing.
    f.rig.set_dark(false);
    f.rig.set_reduce_motion(false);
    f.set("twelve", true);
    f.set("withSeconds", true);
    f.set("refusal", QStringLiteral("That time does not exist on that day, because the clocks go forward."));
    expect_clean(check_everything(f.rig));
}
