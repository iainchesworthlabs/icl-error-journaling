// SPDX-License-Identifier: GPL-3.0-or-later
// ErrorTypeEdit (docs/plan/stage-4.md, 4.13): editor.error_types, the sheet on its own, and the cross-cutting checks.
#include "editors_sheet_support.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;
using namespace ej::ui;
using namespace ej::ui::editors;

namespace
{

void open_for(QQuickItem* sheet, const QString& id, const QString& name, int slot)
{
    REQUIRE(QMetaObject::invokeMethod(sheet, "openFor", Q_ARG(QString, id), Q_ARG(QString, name), Q_ARG(int, slot)));
}

QString id_of_row(World& world, int row)
{
    const QAbstractItemModel* model = world.model().error_types();
    return model->data(model->index(row, 0), test::role_named(*model, "typeId")).toString();
}

} // namespace

TEST_CASE("editor.error_types: the sheet for a new type has a name, the colour it will get and no Retire",
          "[ui][editor]")
{
    World world;
    QQuickItem* sheet = world.load("ErrorTypeEdit");
    REQUIRE(QMetaObject::invokeMethod(sheet, "openForNew"));
    world.play(400);
    world.m_Ui.layout();

    CHECK(sheet->property("isOpen").toBool());
    CHECK(name_of(panel(world)) == QStringLiteral("New error type"));
    CHECK(field_text(world, QStringLiteral("nameField")).isEmpty());
    CHECK_FALSE(in_sheet(world, QStringLiteral("retireRow"))->isVisible());
    // The sample has 11 types: the twelfth gets slot 1 (Sky), and the full board sends it to Other.
    CHECK(in_sheet(world, QStringLiteral("editSwatch"))->isVisible());
    CHECK(in_sheet(world, QStringLiteral("colourName"))->property("text").toString() == QStringLiteral("Sky"));
    CHECK(in_sheet(world, QStringLiteral("colourGroup"))
              ->property("footer")
              .toString()
              .startsWith(QStringLiteral("The board is full")));
    CHECK(in_sheet(world, QStringLiteral("navLeading"))->isVisible());
}

TEST_CASE("editor.error_types: a sheet opened for a position puts the new type there", "[ui][editor]")
{
    World world;
    QQuickItem* sheet = world.load("ErrorTypeEdit");
    REQUIRE(QMetaObject::invokeMethod(sheet, "openForPosition", Q_ARG(int, 3)));
    world.play(400);
    world.m_Ui.layout();

    CHECK(in_sheet(world, QStringLiteral("colourGroup"))->property("footer").toString() ==
          QStringLiteral("It goes on the board at position 3."));
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Echo Delay"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    world.settle();
    CHECK_FALSE(sheet->property("isOpen").toBool());
    const auto& types = world.model().configuration().m_ErrorTypes;
    CHECK(types.back().m_Name == "Echo Delay");
    CHECK(types.back().m_BoardPosition == 3);
    // Wrong Track Played had position 3 and is off the board now.
    CHECK_FALSE(types.at(2).m_BoardPosition.has_value());
}

TEST_CASE("editor.error_types: the sheet for a type shows its name and keeps the colour of its slot", "[ui][editor]")
{
    World world;
    QQuickItem* sheet = world.load("ErrorTypeEdit");
    open_for(sheet, id_of_row(world, 2), QStringLiteral("Wrong Track Played"), 3);
    world.play(400);
    world.m_Ui.layout();

    CHECK(name_of(panel(world)) == QStringLiteral("Error type"));
    CHECK(field_text(world, QStringLiteral("nameField")) == QStringLiteral("Wrong Track Played"));
    CHECK(in_sheet(world, QStringLiteral("retireRow"))->isVisible());
    QQuickItem* swatch = in_sheet(world, QStringLiteral("editSwatch"));
    REQUIRE(swatch->isVisible());
    // The third slot of the light palette: apricot.
    const QColor apricot(world.m_Ui.theme()->property("tileFillsLight").toList().at(2).toString());
    CHECK(swatch->property("color").value<QColor>() == apricot);
    // The row names the colour, from the model: "Apricot", with the note under it.
    CHECK(in_sheet(world, QStringLiteral("colourName"))->property("text").toString() == QStringLiteral("Apricot"));
    CHECK(in_sheet(world, QStringLiteral("colourName"))->property("text").toString() == world.model().colourName(3));
    // The retire button is a card at least 54 pt high, with its label centred.
    QQuickItem* retire = in_sheet(world, QStringLiteral("retireRow"));
    QQuickItem* label = in_sheet(world, QStringLiteral("retireLabel"));
    CHECK(world.m_Ui.geometry(retire).height() >= 54.0);
    CHECK(label->property("horizontalAlignment").toInt() == Qt::AlignHCenter);
}

TEST_CASE("editor.error_types: a refused rename or retire is shown in the sheet, which stays open", "[ui][editor]")
{
    World world;
    QQuickItem* sheet = world.load("ErrorTypeEdit");

    // A type the model does not know: the rename and the retire are both refused with its message.
    open_for(sheet, QStringLiteral("00000000-0000-4000-8000-0000000000ff"), QStringLiteral("Ghost"), 1);
    world.play(400);
    world.m_Ui.layout();
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Ghost Two"));
    tap_in_sheet(world, QStringLiteral("navTrailing"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(sheet->property("refusal").toString().isEmpty());
    CHECK_FALSE(field_error(world, QStringLiteral("nameField")).isEmpty());

    tap_in_sheet(world, QStringLiteral("retireRow"));
    REQUIRE(sheet_is_open(world));
    CHECK_FALSE(sheet->property("retireRefusal").toString().isEmpty());
    CHECK(world.m_Rig.store().saves().empty());
}

TEST_CASE("editor.error_types: Enter in the field saves, like Save", "[ui][editor]")
{
    World world;
    QQuickItem* sheet = world.load("ErrorTypeEdit");
    REQUIRE(QMetaObject::invokeMethod(sheet, "openForNew"));
    world.play(400);
    enter_text(world, QStringLiteral("nameField"), QStringLiteral("Echo Delay"));
    world.m_Ui.key(Qt::Key_Return);
    world.settle();
    world.play(400);

    CHECK_FALSE(sheet->property("isOpen").toBool());
    CHECK(world.model().error_types()->rowCount() == 12);
}

TEST_CASE("editor.voiceover: the type sheet is a dialog with a named field and buttons", "[ui][editor]")
{
    World world;
    QQuickItem* sheet = world.load("ErrorTypeEdit");
    open_for(sheet, id_of_row(world, 0), QStringLiteral("Wrong Response"), 1);
    world.play(400);
    world.m_Ui.layout();

    CHECK(name_of(find_item(panel(world), QStringLiteral("formInput"))) == QStringLiteral("Name"));
    CHECK(name_of(in_sheet(world, QStringLiteral("navLeading"))) == QStringLiteral("Cancel"));
    CHECK(name_of(in_sheet(world, QStringLiteral("navTrailing"))) == QStringLiteral("Save"));
    CHECK(name_of(in_sheet(world, QStringLiteral("retireRow"))) == QStringLiteral("Retire this error type"));
    expect_clean(sheet_violations(world));
    world.m_Ui.set_dark(true);
    expect_clean(sheet_violations(world));
}
