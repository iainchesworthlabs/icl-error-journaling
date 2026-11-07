// SPDX-License-Identifier: GPL-3.0-or-later
// SwipeRow (docs/plan/stage-4.md, 4.6): swipe_row.reveal and swipe_row.accessible.
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

// Twelve rows in a Flickable, so that a vertical drag has a list to scroll. The rows are written out and not made by a
// Repeater, because the rig finds items by walking the objects of the tree and a Repeater's delegates are not in it.
QString list()
{
    QString rows;
    for (int i = 0; i < 12; ++i)
    {
        rows += QStringLiteral(R"(
            SwipeRow {
                objectName: "row%1"
                accessibleName: "10:%2 Mute Delay"
                Text {
                    width: parent.width
                    text: "Mute Delay"
                    color: Theme.colour.textPrimary
                    font: Theme.font.bodySmall.font
                    wrapMode: Text.WordWrap
                }
            })")
                    .arg(i)
                    .arg(10 + i);
    }
    return QStringLiteral(R"(
        Flickable {
            id: flick
            objectName: "flick"
            x: 0
            y: 100
            width: host.width
            height: 400
            contentWidth: width
            contentHeight: rows.height
            clip: true

            Column {
                id: rows
                width: flick.width
                %1
            }
        })")
        .arg(rows);
}

QQuickItem* face_of(UiRig& rig, QQuickItem* row)
{
    return rig.find(QStringLiteral("face"), row);
}

double face_x(UiRig& rig, QQuickItem* row)
{
    return face_of(rig, row)->x();
}

/// A left swipe on the face of `row`, from `from` to `to` (x in the row's coordinates), in the middle of the row.
void swipe_left(UiRig& rig, QQuickItem* row, double from, double to)
{
    QQuickItem* face = face_of(rig, row);
    rig.swipe(face, QPointF(from, face->height() / 2), QPointF(to, face->height() / 2));
}

} // namespace

TEST_CASE("swipe_row.reveal: a swipe past the threshold reveals Copy and Delete, a short one returns",
          "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* row = rig.find(QStringLiteral("row0"));
    REQUIRE(row != nullptr);
    const double reveal = row->property("revealWidth").toDouble();
    // Two buttons of 76 pt at least, as in the design; a long word makes one wider.
    CHECK(reveal >= 152);
    CHECK(reveal < 402 * 0.8);
    CHECK(face_x(rig, row) == 0);

    SECTION("a short swipe goes back")
    {
        swipe_left(rig, row, 300, 270);
        play(rig, 400);
        CHECK_FALSE(row->property("revealed").toBool());
        CHECK(face_x(rig, row) == 0);
    }

    SECTION("a swipe past half of the actions opens the row and stops at the actions")
    {
        swipe_left(rig, row, 340, 140);
        CHECK(row->property("revealed").toBool());
        play(rig, 400);
        CHECK(face_x(rig, row) == -reveal);
        // The actions are in view: the face has left the right-hand 152 pt.
        QQuickItem* copy = rig.find(QStringLiteral("copyAction"), row);
        QQuickItem* del = rig.find(QStringLiteral("deleteAction"), row);
        REQUIRE(copy != nullptr);
        REQUIRE(del != nullptr);
        CHECK(rig.geometry(copy).right() == rig.geometry(del).left());
        CHECK(rig.geometry(del).right() == 402);
    }

    SECTION("a swipe cannot pull the row past the actions or to the right")
    {
        swipe_left(rig, row, 340, 10);
        play(rig, 400);
        CHECK(face_x(rig, row) == -reveal);
        swipe_left(rig, row, 250, 450);
        play(rig, 400);
        CHECK(face_x(rig, row) == 0);
        CHECK_FALSE(row->property("revealed").toBool());
    }
}

TEST_CASE("swipe_row.reveal: a tap on a revealed action emits once, and the row closes", "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("row1"));
    REQUIRE(row != nullptr);
    QSignalSpy copied(row, SIGNAL(copyRequested()));
    QSignalSpy deleted(row, SIGNAL(deleteRequested()));
    QSignalSpy activated(row, SIGNAL(activated()));

    swipe_left(rig, row, 340, 140);
    play(rig, 400);
    REQUIRE(row->property("revealed").toBool());

    rig.tap(rig.find(QStringLiteral("deleteAction"), row));
    CHECK(deleted.count() == 1);
    CHECK(copied.count() == 0);
    CHECK(activated.count() == 0);
    CHECK_FALSE(row->property("revealed").toBool());
    play(rig, 400);
    CHECK(face_x(rig, row) == 0);

    swipe_left(rig, row, 340, 140);
    play(rig, 400);
    rig.tap(rig.find(QStringLiteral("copyAction"), row));
    CHECK(copied.count() == 1);
    CHECK(deleted.count() == 1);
    play(rig, 400);
    CHECK(face_x(rig, row) == 0);
}

TEST_CASE("swipe_row.closed: a tap anywhere on a closed row, the right half included, only activates it", "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("row5"));
    REQUIRE(row != nullptr);
    QSignalSpy activated(row, SIGNAL(activated()));
    QSignalSpy copied(row, SIGNAL(copyRequested()));
    QSignalSpy deleted(row, SIGNAL(deleteRequested()));
    QQuickItem* face = face_of(rig, row);
    REQUIRE(face != nullptr);

    // The hidden buttons are under the right 152 pt or more of the face; the taps run across the whole row.
    int taps = 0;
    for (const double x : {20.0, 100.0, 190.0, 215.0, 260.0, 300.0, 340.0, 395.0})
    {
        rig.tap(face, QPointF(x, 24));
        ++taps;
        CHECK(activated.count() == taps);
        CHECK(copied.count() == 0);
        CHECK(deleted.count() == 0);
    }

    // Right on the places of the two buttons, as seen from the window.
    QQuickItem* copy = rig.find(QStringLiteral("copyAction"), row);
    QQuickItem* del = rig.find(QStringLiteral("deleteAction"), row);
    REQUIRE(copy != nullptr);
    REQUIRE(del != nullptr);
    rig.tap(copy);
    rig.tap(del);
    CHECK(activated.count() == taps + 2);
    CHECK(copied.count() == 0);
    CHECK(deleted.count() == 0);
    CHECK_FALSE(row->property("revealed").toBool());
}

TEST_CASE("swipe_row.closed: the buttons take taps only while the row is open, and a closed row loses them again",
          "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("row6"));
    REQUIRE(row != nullptr);
    QSignalSpy activated(row, SIGNAL(activated()));
    QSignalSpy copied(row, SIGNAL(copyRequested()));
    QSignalSpy deleted(row, SIGNAL(deleteRequested()));

    swipe_left(rig, row, 340, 140);
    play(rig, 400);
    REQUIRE(row->property("revealed").toBool());
    rig.tap(rig.find(QStringLiteral("copyAction"), row));
    CHECK(copied.count() == 1);
    CHECK(deleted.count() == 0);
    CHECK(activated.count() == 0);
    play(rig, 400);

    swipe_left(rig, row, 340, 140);
    play(rig, 400);
    rig.tap(rig.find(QStringLiteral("deleteAction"), row));
    CHECK(copied.count() == 1);
    CHECK(deleted.count() == 1);
    CHECK(activated.count() == 0);
    play(rig, 400);

    // Closed again: a tap where Delete was only activates the row.
    CHECK_FALSE(row->property("revealed").toBool());
    rig.tap(rig.find(QStringLiteral("deleteAction"), row));
    CHECK(deleted.count() == 1);
    CHECK(activated.count() == 1);
}

TEST_CASE("swipe_row.reveal: a tap activates a closed row and only closes an open one", "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("row2"));
    REQUIRE(row != nullptr);
    QSignalSpy activated(row, SIGNAL(activated()));

    rig.tap(face_of(rig, row), QPointF(60, 24));
    CHECK(activated.count() == 1);

    swipe_left(rig, row, 340, 140);
    play(rig, 400);
    rig.tap(face_of(rig, row), QPointF(300, 24));
    CHECK(activated.count() == 1);
    CHECK_FALSE(row->property("revealed").toBool());
    play(rig, 400);
    CHECK(face_x(rig, row) == 0);
}

TEST_CASE("swipe_row.reveal: a vertical drag scrolls the list and does not open the row", "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* flick = rig.find(QStringLiteral("flick"));
    QQuickItem* row = rig.find(QStringLiteral("row3"));
    REQUIRE(flick != nullptr);
    REQUIRE(row != nullptr);
    QSignalSpy activated(row, SIGNAL(activated()));
    REQUIRE(flick->property("contentY").toDouble() == 0);

    QQuickItem* face = face_of(rig, row);
    // Down the list with a little drift to the left, which is how a thumb scrolls.
    rig.drag(face, QPointF(200, 40), QPointF(185, -100), 10);
    play(rig, 400);
    CHECK(flick->property("contentY").toDouble() > 0);
    CHECK_FALSE(row->property("revealed").toBool());
    CHECK(face_x(rig, row) == 0);
    CHECK(activated.count() == 0);
}

TEST_CASE("swipe_row.reveal: with Reduce Motion the row jumps and nothing slides", "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    rig.set_reduce_motion(true);
    QQuickItem* row = rig.find(QStringLiteral("row0"));
    REQUIRE(row != nullptr);

    swipe_left(rig, row, 340, 140);
    CHECK(face_x(rig, row) == -row->property("revealWidth").toDouble());
    expect_clean(check_motion_reduced(rig));

    rig.tap(face_of(rig, row), QPointF(300, 24));
    CHECK(face_x(rig, row) == 0);
}

TEST_CASE("swipe_row.accessible: the row is a button, and Copy and Delete are reachable without the swipe",
          "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("row4"));
    REQUIRE(row != nullptr);
    QSignalSpy activated(row, SIGNAL(activated()));
    QSignalSpy copied(row, SIGNAL(copyRequested()));
    QSignalSpy deleted(row, SIGNAL(deleteRequested()));

    QQuickItem* face = face_of(rig, row);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(face);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::Button);
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("10:14 Mute Delay"));
    iface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(activated.count() == 1);

    // The actions are in the accessibility tree while the row is closed, named after the row.
    QStringList names;
    for (const Node& node : rig.accessible_tree(row))
    {
        if (node.m_Role == QAccessible::Button)
        {
            names.push_back(node.m_Name);
        }
    }
    CHECK(names == QStringList{QStringLiteral("10:14 Mute Delay"), QStringLiteral("Copy, 10:14 Mute Delay"),
                               QStringLiteral("Delete, 10:14 Mute Delay")});

    QAccessibleInterface* copy = QAccessible::queryAccessibleInterface(rig.find(QStringLiteral("copyAction"), row));
    QAccessibleInterface* del = QAccessible::queryAccessibleInterface(rig.find(QStringLiteral("deleteAction"), row));
    REQUIRE(copy != nullptr);
    REQUIRE(del != nullptr);
    CHECK(copy->role() == QAccessible::Button);
    copy->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(copied.count() == 1);
    del->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    CHECK(deleted.count() == 1);
    CHECK(activated.count() == 1);
    // Both were pressed on a closed row, which stays closed: the press action does not need the swipe.
    CHECK_FALSE(row->property("revealed").toBool());

    face->forceActiveFocus();
    rig.key(Qt::Key_Return);
    CHECK(activated.count() == 2);
}

TEST_CASE("swipe_row.colours: Copy is on the blue and Delete on the destructive fill, in light and dark",
          "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("row0"));
    REQUIRE(row != nullptr);
    QQuickItem* copy = rig.find(QStringLiteral("copyAction"), row);
    QQuickItem* del = rig.find(QStringLiteral("deleteAction"), row);
    REQUIRE(copy != nullptr);
    REQUIRE(del != nullptr);
    // The inline component's first rectangle is its fill (a TextMetrics comes first but is not an item).
    CHECK(fill_of(copy) == theme_colour(rig, "primary"));
    CHECK(fill_of(del) == theme_colour(rig, "destructiveFill"));
    rig.set_dark(true);
    CHECK(fill_of(copy) == theme_colour(rig, "primary"));
    CHECK(fill_of(del) == theme_colour(rig, "destructiveFill"));
    CHECK(fill_of(del) != QColor(QStringLiteral("#B42318")));
}

TEST_CASE("swipe_row.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][swipe_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, list()) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}
