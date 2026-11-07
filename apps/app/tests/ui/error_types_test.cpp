// SPDX-License-Identifier: GPL-3.0-or-later
// ErrorTypes and its sheet ErrorTypeEdit (docs/plan/stage-4.md, 4.13): editor.error_types, editor.voiceover and the
// cross-cutting checks.
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

constexpr auto TYPES = "typeRow";

/// Opens the sheet of the row named `name` by tapping the row, and lets it be presented.
void open_row(World& world, const QString& name)
{
    QQuickItem* row = row_with(world, TYPES, name);
    REQUIRE(row != nullptr);
    world.m_Ui.tap(label_of(row));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
}

QStringList names(World& world)
{
    return row_names(world, TYPES);
}

} // namespace

TEST_CASE("editor.error_types: the types are listed in board order with their tile, and a type off the board is apart",
          "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();

    const QStringList listed = names(world);
    REQUIRE(listed.size() == 11);
    CHECK(listed.front() == QStringLiteral("Wrong Response"));
    CHECK(listed.at(1) == QStringLiteral("Sequence Out of Order"));
    CHECK(listed.back() == QStringLiteral("Navigation Failure"));

    QQuickItem* first = row_with(world, TYPES, QStringLiteral("Wrong Response"));
    REQUIRE(first != nullptr);
    CHECK(first->property("detail").toString() == QStringLiteral("Board tile 1"));
    CHECK(first->property("swatchSlot").toInt() == 1);
    CHECK(item(world, QStringLiteral("otherGroup"))->isVisible() == false);

    // A twelfth type is on no tile: it is listed under its own heading, after the board's.
    REQUIRE(world.model().addErrorType(QStringLiteral("Echo Delay")).isEmpty());
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world).size() == 12);
    CHECK(names(world).back() == QStringLiteral("Echo Delay"));
    CHECK(item(world, QStringLiteral("otherGroup"))->isVisible());
    QQuickItem* twelfth = row_with(world, TYPES, QStringLiteral("Echo Delay"));
    CHECK(twelfth->property("detail").toString() == QStringLiteral("Not on the board"));
    CHECK(find_item(twelfth, QStringLiteral("editorMoveEarlier"))->isVisible() == false);
}

TEST_CASE("editor.error_types: adding a type asks for a name, refuses a duplicate or empty one inline, then saves",
          "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();

    world.m_Ui.tap(label_of(item(world, QStringLiteral("addRow"))));
    world.play(400);
    world.m_Ui.layout();
    REQUIRE(sheet_is_open(world));
    CHECK(in_sheet(world, QStringLiteral("retireRow"))->isVisible() == false);

    // An empty name is refused, with the model's message under the field, and Save is dimmed until the name is edited.
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(field_error(world, QStringLiteral("nameField")).isEmpty());
    CHECK_FALSE(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());

    // A name that another type has, in other capitals, is refused too, and the list is unchanged.
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("mute delay"));
    CHECK(field_error(world, QStringLiteral("nameField")).isEmpty());
    CHECK(in_sheet(world, QStringLiteral("navTrailing"))->isEnabled());
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK(field_error(world, QStringLiteral("nameField")).contains(QStringLiteral("Mute Delay"), Qt::CaseInsensitive));
    CHECK(names(world).size() == 11);

    // A good name is saved, and the sheet closes.
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Echo Delay"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world).size() == 12);
    CHECK(names(world).back() == QStringLiteral("Echo Delay"));
}

TEST_CASE("editor.error_types: a name of 200 characters is saved and one of 201 is refused", "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();
    world.m_Ui.tap(label_of(item(world, QStringLiteral("addRow"))));
    world.play(400);

    enter_text(world, QStringLiteral("nameField"), QString(201, QLatin1Char('a')));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(field_error(world, QStringLiteral("nameField")).isEmpty());
    CHECK(names(world).size() == 11);

    enter_text(world, QStringLiteral("nameField"), QString(200, QLatin1Char('a')));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(names(world).size() == 12);
}

TEST_CASE("editor.error_types: a row opens its name for renaming, and Cancel changes nothing", "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();

    open_row(world, QStringLiteral("Mute Delay"));
    CHECK(field_text(world, QStringLiteral("nameField")) == QStringLiteral("Mute Delay"));
    CHECK(in_sheet(world, QStringLiteral("retireRow"))->isVisible());
    CHECK(in_sheet(world, QStringLiteral("editSwatch"))->isVisible());

    // Cancel leaves the type as it was.
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Mute Lag"));
    tap_in_sheet(world, QStringLiteral("navLeading"));
    CHECK_FALSE(sheet_is_open(world));
    CHECK(row_with(world, TYPES, QStringLiteral("Mute Delay")) != nullptr);
    CHECK(world.m_Rig.store().saves().empty());

    // Save renames it, and it keeps its tile and its colour.
    open_row(world, QStringLiteral("Mute Delay"));
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Mute Lag"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    CHECK_FALSE(sheet_is_open(world));
    QQuickItem* renamed = row_with(world, TYPES, QStringLiteral("Mute Lag"));
    REQUIRE(renamed != nullptr);
    CHECK(row_with(world, TYPES, QStringLiteral("Mute Delay")) == nullptr);
    CHECK(renamed->property("detail").toString() == QStringLiteral("Board tile 8"));
    CHECK(renamed->property("swatchSlot").toInt() == 8);
}

TEST_CASE("editor.error_types: retiring a type removes it from the list and the board", "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();

    open_row(world, QStringLiteral("Mute Delay"));
    tap_in_sheet(world, QStringLiteral("retireRow"));
    CHECK_FALSE(sheet_is_open(world));
    const QStringList listed = names(world);
    CHECK(listed.size() == 10);
    CHECK_FALSE(listed.contains(QStringLiteral("Mute Delay")));

    // The model keeps it, retired, so past entries still name it.
    const auto& types = world.model().configuration().m_ErrorTypes;
    int retired = 0;
    for (const auto& type : types)
    {
        retired += type.m_Retired ? 1 : 0;
    }
    CHECK(retired == 1);
}

TEST_CASE("editor.error_types: the arrows move a type along the board and its colour stays", "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();

    QQuickItem* first = row_with(world, TYPES, QStringLiteral("Wrong Response"));
    REQUIRE(first != nullptr);
    // The first type cannot move earlier and the arrow is dimmed.
    CHECK_FALSE(find_item(first, QStringLiteral("editorMoveEarlier"))->isEnabled());
    CHECK(find_item(first, QStringLiteral("editorMoveLater"))->isEnabled());

    world.m_Ui.tap(find_item(first, QStringLiteral("editorMoveLater")));
    world.settle();
    world.m_Ui.layout();
    const QStringList moved = names(world);
    CHECK(moved.at(0) == QStringLiteral("Sequence Out of Order"));
    CHECK(moved.at(1) == QStringLiteral("Wrong Response"));
    QQuickItem* row = row_with(world, TYPES, QStringLiteral("Wrong Response"));
    CHECK(row->property("detail").toString() == QStringLiteral("Board tile 2"));
    CHECK(row->property("swatchSlot").toInt() == 1);

    // And back again, with the arrow of that row.
    world.m_Ui.tap(find_item(row, QStringLiteral("editorMoveEarlier")));
    world.settle();
    world.m_Ui.layout();
    CHECK(names(world).front() == QStringLiteral("Wrong Response"));

    // The last type cannot move later.
    QQuickItem* last = row_with(world, TYPES, QStringLiteral("Navigation Failure"));
    CHECK_FALSE(find_item(last, QStringLiteral("editorMoveLater"))->isEnabled());
}

TEST_CASE("editor.error_types: a refused move and a failed save are shown in a banner", "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();
    QQuickItem* banner = item(world, QStringLiteral("problemBanner"));
    REQUIRE(banner != nullptr);
    CHECK_FALSE(banner->isVisible());

    // A second change while the first is being saved is refused with the model's message.
    QQuickItem* row = row_with(world, TYPES, QStringLiteral("Wrong Response"));
    world.m_Ui.tap(find_item(row, QStringLiteral("editorMoveLater")));
    world.m_Ui.tap(find_item(row, QStringLiteral("editorMoveLater")));
    CHECK(banner->isVisible());
    CHECK(banner->property("message").toString().contains(QStringLiteral("still being saved")));
    world.settle();

    // A save that fails says so, and the list is unchanged.
    QMetaObject::invokeMethod(banner, "closeRequested");
    CHECK_FALSE(banner->isVisible());
    world.m_Rig.store().fail_next_save(make_error(StorageErrorKind::DISK_FULL, "no room"));
    const QStringList before = names(world);
    world.m_Ui.tap(
        find_item(row_with(world, TYPES, QStringLiteral("Wrong Track Played")), QStringLiteral("editorMoveLater")));
    world.settle();
    CHECK(banner->isVisible());
    CHECK(names(world) == before);
}

TEST_CASE("editor.error_types: the Settings and Done buttons ask to close", "[ui][editor]")
{
    World world;
    QQuickItem* screen = world.load("ErrorTypes");
    world.m_Ui.layout();
    QSignalSpy closed(screen, SIGNAL(closeRequested()));
    world.m_Ui.tap(find_item(screen, QStringLiteral("navLeading")));
    world.m_Ui.tap(find_item(screen, QStringLiteral("navTrailing")));
    CHECK(closed.count() == 2);
}

TEST_CASE("editor.voiceover: rows and buttons have names, the sheet is a dialog, and the screen passes the checks",
          "[ui][editor]")
{
    World world;
    world.load("ErrorTypes");
    world.m_Ui.layout();

    QQuickItem* row = row_with(world, TYPES, QStringLiteral("Wrong Response"));
    REQUIRE(row != nullptr);
    CHECK(name_of(label_of(row)) == QStringLiteral("Wrong Response, Board tile 1"));
    CHECK(name_of(find_item(row, QStringLiteral("editorMoveLater"))) == QStringLiteral("Move Wrong Response later"));
    CHECK(name_of(find_item(row, QStringLiteral("editorMoveEarlier"))) ==
          QStringLiteral("Move Wrong Response earlier"));
    CHECK(name_of(label_of(item(world, QStringLiteral("addRow")))) == QStringLiteral("Add an error type"));

    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(true);
    expect_clean(check_everything(world.m_Ui));
    world.m_Ui.set_dark(false);

    // With the sheet open.
    open_row(world, QStringLiteral("Wrong Response"));
    CHECK(name_of(panel(world)) == QStringLiteral("Error type"));
    expect_clean(sheet_violations(world));
}
