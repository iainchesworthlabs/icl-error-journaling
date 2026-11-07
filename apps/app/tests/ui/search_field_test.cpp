// SPDX-License-Identifier: GPL-3.0-or-later
// SearchField (docs/plan/stage-4.md, 4.6): search_field.filter, and the placeholder contrast that 4.1 measured.
#include "input_components_support.hpp"
#include "rig/color_math.hpp"
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

// The host plays the part of the picker: it holds the filter and stores what the user typed.
constexpr auto PICKER = R"(
    property string filter: ""
    SearchField {
        id: search
        objectName: "search"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        filter: host.filter
        placeholderText: "Search error types"
        onFilterEdited: text => host.filter = text
    })";

QStringList texts(QSignalSpy& spy)
{
    QStringList out;
    for (const QList<QVariant>& arguments : spy)
    {
        out.push_back(arguments.at(0).toString());
    }
    return out;
}

} // namespace

TEST_CASE("search_field.filter: typing emits filterEdited once for each change", "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* search = rig.find(QStringLiteral("search"));
    QQuickItem* input = rig.find(QStringLiteral("searchInput"));
    REQUIRE(search != nullptr);
    REQUIRE(input != nullptr);
    QSignalSpy edited(search, SIGNAL(filterEdited(QString)));
    QSignalSpy changed(search, SIGNAL(filterChanged()));

    input->forceActiveFocus();
    rig.type(QStringLiteral("mute"));
    CHECK(texts(edited) ==
          QStringList{QStringLiteral("m"), QStringLiteral("mu"), QStringLiteral("mut"), QStringLiteral("mute")});
    // The screen stored each one; the field reports each change of the filter once.
    CHECK(search->property("filter").toString() == QStringLiteral("mute"));
    CHECK(changed.count() == 4);
    CHECK(input->property("text").toString() == QStringLiteral("mute"));

    rig.key(Qt::Key_Backspace);
    CHECK(edited.count() == 5);
    CHECK(edited.last().at(0).toString() == QStringLiteral("mut"));
}

TEST_CASE("search_field.filter: a filter set from outside shows in the field and is not reported as typed",
          "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);
    QQuickItem* search = rig.find(QStringLiteral("search"));
    QQuickItem* input = rig.find(QStringLiteral("searchInput"));
    REQUIRE(search != nullptr);
    REQUIRE(input != nullptr);
    QSignalSpy edited(search, SIGNAL(filterEdited(QString)));

    require_root(rig)->setProperty("filter", QStringLiteral("sequence"));
    CHECK(input->property("text").toString() == QStringLiteral("sequence"));
    CHECK(edited.count() == 0);
}

TEST_CASE("search_field.filter: the clear button appears with text, empties the field and reports it once",
          "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);
    QQuickItem* search = rig.find(QStringLiteral("search"));
    QQuickItem* input = rig.find(QStringLiteral("searchInput"));
    QQuickItem* clear = rig.find(QStringLiteral("clearButton"));
    REQUIRE(search != nullptr);
    REQUIRE(input != nullptr);
    REQUIRE(clear != nullptr);
    QSignalSpy edited(search, SIGNAL(filterEdited(QString)));

    CHECK_FALSE(clear->isVisible());
    input->forceActiveFocus();
    rig.type(QStringLiteral("mute"));
    REQUIRE(clear->isVisible());
    CHECK(rig.geometry(clear).width() >= 44);
    CHECK(rig.geometry(clear).height() >= 44);
    edited.clear();

    rig.tap(clear);
    CHECK(edited.count() == 1);
    CHECK(edited.at(0).at(0).toString().isEmpty());
    CHECK(input->property("text").toString().isEmpty());
    CHECK(search->property("filter").toString().isEmpty());
    CHECK_FALSE(clear->isVisible());
    // The keyboard stays: the field keeps the focus.
    CHECK(input->hasActiveFocus());
}

TEST_CASE("search_field.accessibility: an edit field with a search name, and a named clear button",
          "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);
    QQuickItem* input = rig.find(QStringLiteral("searchInput"));
    QQuickItem* clear = rig.find(QStringLiteral("clearButton"));
    REQUIRE(input != nullptr);
    QAccessibleInterface* field = QAccessible::queryAccessibleInterface(input);
    REQUIRE(field != nullptr);
    CHECK(field->role() == QAccessible::EditableText);
    CHECK(field->text(QAccessible::Name) == QStringLiteral("Search error types"));

    input->forceActiveFocus();
    rig.type(QStringLiteral("x"));
    QAccessibleInterface* button = QAccessible::queryAccessibleInterface(clear);
    REQUIRE(button != nullptr);
    CHECK(button->role() == QAccessible::Button);
    CHECK(button->text(QAccessible::Name) == QStringLiteral("Clear search"));
    press_action(clear);
    CHECK(input->property("text").toString().isEmpty());
}

TEST_CASE("search_field.geometry: 44 pt high with 12 pt corners, and taller with larger text", "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);
    QQuickItem* search = rig.find(QStringLiteral("search"));
    QQuickItem* input = rig.find(QStringLiteral("searchInput"));
    REQUIRE(search != nullptr);
    REQUIRE(input != nullptr);
    CHECK(rig.geometry(search).height() == 44);
    CHECK(rig.geometry(search).width() == 402 - 32);
    const QObject* background = input->property("background").value<QObject*>();
    REQUIRE(background != nullptr);
    CHECK(background->property("radius").toDouble() == 12);
    CHECK(background->property("color").value<QColor>() == theme_colour(rig, "searchBackground"));

    rig.set_text_scale(3.1);
    CHECK(rig.geometry(search).height() > 44);
}

TEST_CASE(
    "search_field.placeholder: the contrast of the placeholder on the search field, which is a known gap in light",
    "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);

    // The light placeholder is the approved #6B7A86. On the white card it is about 4.4 to 1, on the field colour
    // about 4.2 to 1 (4.1 measured it, and Theme.contrastPairs lists both as exceptions) and on the grey of this field
    // about 3.6 to 1, which Theme.contrastPairs does not list. These are the figures today; they must not get worse,
    // and a darker placeholder token fixes all three.
    const double light = contrast_ratio(theme_colour(rig, "textPlaceholder"), theme_colour(rig, "searchBackground"));
    INFO("light placeholder on the search field: " << light);
    CHECK(light >= 3.5);
    CHECK(light < 4.5);

    rig.set_dark(true);
    const double dark = contrast_ratio(theme_colour(rig, "textPlaceholder"), theme_colour(rig, "searchBackground"));
    INFO("dark placeholder on the search field: " << dark);
    CHECK(dark >= 4.5);
}

TEST_CASE("search_field.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][search_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(PICKER)) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));

    // With text in it, and so with the clear button.
    rig.set_reduce_motion(false);
    require_root(rig)->setProperty("filter", QStringLiteral("Sequence Out of Order"));
    rig.layout();
    expect_clean(check_everything(rig));
}
