// SPDX-License-Identifier: GPL-3.0-or-later
// FormField (docs/plan/stage-4.md, 4.6): form_field.validation, and the field states.
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

// The host plays the part of the details sheet: it holds the value, asks for a refusal, and shows the Save state.
constexpr auto VERSION = R"(
    property string value: "1.4.2"
    property string refusal: ""
    readonly property bool canSave: !field.invalid
    FormField {
        id: field
        objectName: "field"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        label: "Software version"
        hint: "Format 1.4.2, numbers only."
        text: host.value
        errorText: host.refusal
        valid: host.refusal.length === 0 && host.value.length > 0
        onTextEdited: text => host.value = text
    })";

constexpr auto UNIT = R"(
    FormField {
        id: field
        objectName: "field"
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin
        label: "UNIT number"
        prefixText: "UNIT-"
        hint: "Type the number only."
        inputMethodHints: Qt.ImhDigitsOnly
    })";

QQuickItem* text_item(UiRig& rig, const QString& text)
{
    for (const Node& node : rig.tree())
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) && node.m_Visible &&
            node.m_Item->property("text").toString() == text)
        {
            return node.m_Item;
        }
    }
    return nullptr;
}

/// The thin rotated rectangles that are shown: the arms of a check mark.
int check_arms(UiRig& rig)
{
    int count = 0;
    for (const Node& node : rig.tree())
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) && node.m_Visible &&
            node.m_Item->rotation() != 0)
        {
            ++count;
        }
    }
    return count;
}

} // namespace

TEST_CASE("form_field.validation: the refusal appears inline, is the field's description, and the Save state follows",
          "[ui][form_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(VERSION)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* field = rig.find(QStringLiteral("field"));
    QQuickItem* input = rig.find(QStringLiteral("formInput"));
    REQUIRE(field != nullptr);
    REQUIRE(input != nullptr);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(input);
    REQUIRE(iface != nullptr);

    // Valid: the hint is shown, the description is the hint, Save is allowed and the check mark shows.
    CHECK(text_item(rig, QStringLiteral("Format 1.4.2, numbers only.")) != nullptr);
    CHECK(iface->text(QAccessible::Description) == QStringLiteral("Format 1.4.2, numbers only."));
    CHECK(rig.root()->property("canSave").toBool());
    CHECK(check_arms(rig) == 2);
    CHECK(input->property("background")
              .value<QObject*>()
              ->property("border")
              .value<QObject*>()
              ->property("width")
              .toDouble() == 0.0);

    // Refused: the model's message replaces the hint under the field and is the description.
    const QString message = QStringLiteral("A version has three numbers, such as 1.4.2.");
    rig.root()->setProperty("value", QStringLiteral("1.4"));
    rig.root()->setProperty("refusal", message);
    rig.layout();
    QQuickItem* shown = text_item(rig, message);
    REQUIRE(shown != nullptr);
    CHECK(text_item(rig, QStringLiteral("Format 1.4.2, numbers only.")) == nullptr);
    CHECK(shown->property("color").value<QColor>() == theme_colour(rig, "destructive"));
    CHECK(rig.geometry(shown).top() >= rig.geometry(input).bottom());
    CHECK(iface->text(QAccessible::Description) == message);
    CHECK(field->property("invalid").toBool());
    CHECK_FALSE(rig.root()->property("canSave").toBool());
    // Not colour alone: the message is text, the border is 2 pt, and the check mark is gone.
    CHECK(check_arms(rig) == 0);
    const QObject* background = input->property("background").value<QObject*>();
    REQUIRE(background != nullptr);
    CHECK(background->property("border").value<QObject*>()->property("width").toDouble() == 2.0);

    // Corrected: the hint returns and Save is allowed again.
    rig.root()->setProperty("value", QStringLiteral("1.4.2"));
    rig.root()->setProperty("refusal", QString());
    rig.layout();
    CHECK(text_item(rig, message) == nullptr);
    CHECK(rig.root()->property("canSave").toBool());
}

TEST_CASE("form_field.validation: the message is read with a long refusal at the largest text size and is never cut",
          "[ui][form_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(VERSION)) != nullptr);
    rig.root()->setProperty("refusal",
                            QStringLiteral("A software version has three numbers separated by dots, such as 1.4.2."));
    rig.set_text_scale(3.1);
    expect_clean(check_text_scale(rig));
    QQuickItem* field = rig.find(QStringLiteral("field"));
    REQUIRE(field != nullptr);
    CHECK(rig.geometry(field).height() > 200);
}

TEST_CASE(
    "form_field.text: typing reports each change, a value from outside is shown, and the prefix is not in the text",
    "[ui][form_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(UNIT)) != nullptr);
    QQuickItem* field = rig.find(QStringLiteral("field"));
    QQuickItem* input = rig.find(QStringLiteral("formInput"));
    REQUIRE(field != nullptr);
    QSignalSpy edited(field, SIGNAL(textEdited(QString)));

    // The prefix is drawn, and the number pad is asked for.
    CHECK(text_item(rig, QStringLiteral("UNIT-")) != nullptr);
    CHECK(input->property("inputMethodHints").toInt() == static_cast<int>(Qt::ImhDigitsOnly));

    input->forceActiveFocus();
    rig.type(QStringLiteral("27"));
    REQUIRE(edited.count() == 2);
    CHECK(edited.at(0).at(0).toString() == QStringLiteral("2"));
    CHECK(edited.at(1).at(0).toString() == QStringLiteral("27"));
    CHECK(input->property("text").toString() == QStringLiteral("27"));

    field->setProperty("text", QStringLiteral("28"));
    CHECK(input->property("text").toString() == QStringLiteral("28"));
    CHECK(edited.count() == 2);
}

TEST_CASE("form_field.accessibility: the label is the name of an edit field, which is not read twice",
          "[ui][form_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(UNIT)) != nullptr);
    QQuickItem* input = rig.find(QStringLiteral("formInput"));
    REQUIRE(input != nullptr);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(input);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::EditableText);
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("UNIT number"));
    CHECK(iface->text(QAccessible::Description) == QStringLiteral("Type the number only."));

    // The visible label (capitals), prefix and message are the field's name and description, so they are not elements
    // too.
    QStringList names;
    for (const Node& node : rig.accessible_tree())
    {
        if (node.m_Role != QAccessible::NoRole && node.m_Item != rig.root())
        {
            names.push_back(node.m_Name);
        }
    }
    CHECK(names == QStringList{QStringLiteral("UNIT number")});
}

TEST_CASE("form_field.states: the field has a 14 pt card, a border while it has the focus, and no label when asked",
          "[ui][form_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(UNIT)) != nullptr);
    QQuickItem* input = rig.find(QStringLiteral("formInput"));
    REQUIRE(input != nullptr);
    const QObject* background = input->property("background").value<QObject*>();
    REQUIRE(background != nullptr);
    CHECK(background->property("radius").toDouble() == 14);
    CHECK(background->property("color").value<QColor>() == theme_colour(rig, "surface"));
    CHECK(background->property("border").value<QObject*>()->property("width").toDouble() == 0.0);

    input->forceActiveFocus();
    CHECK(background->property("border").value<QObject*>()->property("width").toDouble() == 2.0);
    CHECK(background->property("border").value<QObject*>()->property("color").value<QColor>() ==
          theme_colour(rig, "primaryText"));
    CHECK(rig.geometry(input).height() >= 44);

    CHECK(text_item(rig, QStringLiteral("UNIT NUMBER")) != nullptr);
    rig.find(QStringLiteral("field"))->setProperty("labelVisible", false);
    rig.layout();
    CHECK(text_item(rig, QStringLiteral("UNIT NUMBER")) == nullptr);
}

TEST_CASE(
    "form_field.placeholder: the placeholder is the token that 4.1 measured at about 4.2 to 1 on the field in light",
    "[ui][form_field]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(UNIT)) != nullptr);
    // The figures today (Theme.contrastPairs lists the first two as exceptions): they must not get worse.
    const QColor placeholder = theme_colour(rig, "textPlaceholder");
    CHECK(contrast_ratio(placeholder, theme_colour(rig, "fieldBackground")) >= 4.1);
    CHECK(contrast_ratio(placeholder, theme_colour(rig, "surface")) >= 4.4);
    rig.set_dark(true);
    CHECK(contrast_ratio(theme_colour(rig, "textPlaceholder"), theme_colour(rig, "fieldBackground")) >= 4.5);
    CHECK(contrast_ratio(theme_colour(rig, "textPlaceholder"), theme_colour(rig, "surface")) >= 4.5);
}

TEST_CASE("form_field.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][form_field]")
{
    for (const char* source : {VERSION, UNIT})
    {
        UiRig rig;
        REQUIRE(load_host(rig, QString::fromUtf8(source)) != nullptr);
        expect_clean(check_everything(rig));
        rig.set_dark(true);
        expect_clean(check_everything(rig));
        rig.set_reduce_motion(true);
        expect_clean(check_motion_reduced(rig));
    }
}
