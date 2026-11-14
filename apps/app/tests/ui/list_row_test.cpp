// SPDX-License-Identifier: GPL-3.0-or-later
// ListRow (docs/plan/stage-4.md, 4.6): list_row.layout, and the row states.
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

constexpr auto ROWS = R"(
    GroupedList {
        x: Theme.space.screenMargin
        y: 100
        width: host.width - 2 * Theme.space.screenMargin

        ListRow { objectName: "first"; text: "North Site"; checkable: true; checked: true }
        ListRow { objectName: "second"; text: "South Site"; checkable: true }
        ListRow { objectName: "third"; text: "Software version"; value: "1.4.2"; showChevron: true }
        ListRow { objectName: "fourth"; text: "Retire this error type"; destructive: true }
        ListRow { objectName: "fixed"; text: "Tester"; value: "Sam Example"; tappable: false }
    })";

/// The rule lines under the content of `row`: visible rectangles one hairline high.
int separators(UiRig& rig, QQuickItem* row)
{
    int count = 0;
    for (const Node& node : rig.tree(row))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickRectangle")) && node.m_Visible &&
            node.m_Rect.height() == 1)
        {
            ++count;
        }
    }
    return count;
}

/// The marks of a row that are shown: the check mark or the chevron (an Icon of kind "check" or "chevronRight").
int mark_arms(UiRig& rig, QQuickItem* row)
{
    int count = 0;
    for (const Node& node : rig.tree(row))
    {
        const QString kind = node.m_Item->property("kind").toString();
        if (node.m_Visible && (kind == QStringLiteral("check") || kind == QStringLiteral("chevronRight")))
        {
            ++count;
        }
    }
    return count;
}

QQuickItem* text_item(UiRig& rig, QQuickItem* row, const QString& text)
{
    for (const Node& node : rig.tree(row))
    {
        if (node.m_TypeName.startsWith(QStringLiteral("QQuickText")) &&
            node.m_Item->property("text").toString() == text)
        {
            return node.m_Item;
        }
    }
    return nullptr;
}

} // namespace

TEST_CASE("list_row.layout: 48 pt high at least, the width of the group, and taller when the text wraps",
          "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    INFO(rig.errors().toStdString());
    QQuickItem* first = rig.find(QStringLiteral("first"));
    QQuickItem* third = rig.find(QStringLiteral("third"));
    REQUIRE(first != nullptr);
    REQUIRE(third != nullptr);

    CHECK(rig.geometry(first).height() == 48);
    CHECK(rig.geometry(first).width() == 402 - 32);
    CHECK(rig.geometry(third).height() == 48);

    rig.set_text_scale(3.1);
    CHECK(rig.geometry(first).height() > 48);
    CHECK(rig.geometry(third).height() >= rig.geometry(first).height());
    expect_clean(check_text_scale(rig));
}

TEST_CASE("list_row.layout: a rule under every row but the last of the group", "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    CHECK(separators(rig, rig.find(QStringLiteral("first"))) == 1);
    CHECK(separators(rig, rig.find(QStringLiteral("fourth"))) == 1);
    CHECK(separators(rig, rig.find(QStringLiteral("fixed"))) == 0);
    CHECK(require_item(rig, QStringLiteral("first"))->property("isFirst").toBool());
    CHECK_FALSE(require_item(rig, QStringLiteral("first"))->property("isLast").toBool());
    CHECK(require_item(rig, QStringLiteral("fixed"))->property("isLast").toBool());

    require_item(rig, QStringLiteral("fourth"))->setProperty("showSeparator", false);
    CHECK(separators(rig, rig.find(QStringLiteral("fourth"))) == 0);
}

TEST_CASE("list_row.activate: a tap emits activated once; a row that is not tappable is a label", "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    QQuickItem* second = rig.find(QStringLiteral("second"));
    QQuickItem* fixed = rig.find(QStringLiteral("fixed"));
    REQUIRE(second != nullptr);
    REQUIRE(fixed != nullptr);
    QSignalSpy activated(second, SIGNAL(activated()));
    QSignalSpy fixed_activated(fixed, SIGNAL(activated()));

    const int finger = rig.press(second);
    CHECK(activated.count() == 0);
    CHECK(second->property("pressed").toBool());
    rig.release(finger);
    CHECK(activated.count() == 1);

    rig.tap(fixed);
    CHECK(fixed_activated.count() == 0);

    QAccessibleInterface* label = QAccessible::queryAccessibleInterface(fixed);
    REQUIRE(label != nullptr);
    CHECK(label->role() == QAccessible::StaticText);
    CHECK(label->text(QAccessible::Name) == QStringLiteral("Tester, Sam Example"));
    for (const Node& node : rig.tree())
    {
        if (node.m_Item == fixed)
        {
            CHECK_FALSE(node.m_Interactive);
        }
    }
}

TEST_CASE("list_row.marks: a check on the chosen row, a chevron on a row that opens a page, neither on the rest",
          "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    CHECK(mark_arms(rig, rig.find(QStringLiteral("first"))) == 1);
    CHECK(mark_arms(rig, rig.find(QStringLiteral("second"))) == 0);
    CHECK(mark_arms(rig, rig.find(QStringLiteral("third"))) == 1);
    CHECK(mark_arms(rig, rig.find(QStringLiteral("fourth"))) == 0);

    // The state is also spoken, so the check is not the only sign of it.
    QAccessibleInterface* first = QAccessible::queryAccessibleInterface(rig.find(QStringLiteral("first")));
    QAccessibleInterface* second = QAccessible::queryAccessibleInterface(rig.find(QStringLiteral("second")));
    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    CHECK(first->state().checkable);
    CHECK(first->state().checked);
    CHECK(second->state().checkable);
    CHECK_FALSE(second->state().checked);

    require_item(rig, QStringLiteral("second"))->setProperty("checked", true);
    require_item(rig, QStringLiteral("first"))->setProperty("checked", false);
    CHECK(mark_arms(rig, rig.find(QStringLiteral("first"))) == 0);
    CHECK(mark_arms(rig, rig.find(QStringLiteral("second"))) == 1);
}

TEST_CASE("list_row.states: a destructive row has its label in the destructive colour, in light and dark",
          "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    QQuickItem* row = rig.find(QStringLiteral("fourth"));
    QQuickItem* normal = rig.find(QStringLiteral("second"));
    REQUIRE(row != nullptr);
    REQUIRE(normal != nullptr);
    QQuickItem* label = text_item(rig, row, QStringLiteral("Retire this error type"));
    QQuickItem* plain = text_item(rig, normal, QStringLiteral("South Site"));
    REQUIRE(label != nullptr);
    REQUIRE(plain != nullptr);
    CHECK(label->property("color").value<QColor>() == theme_colour(rig, "destructive"));
    CHECK(plain->property("color").value<QColor>() == theme_colour(rig, "textPrimary"));
    rig.set_dark(true);
    CHECK(label->property("color").value<QColor>() == theme_colour(rig, "destructive"));
    CHECK(label->property("color").value<QColor>() != QColor(QStringLiteral("#B42318")));
}

TEST_CASE(
    "list_row.voiceover: a tappable row is a button that reads its label and value, and the press action activates it",
    "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    QQuickItem* third = rig.find(QStringLiteral("third"));
    REQUIRE(third != nullptr);
    QSignalSpy activated(third, SIGNAL(activated()));
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(third);
    REQUIRE(iface != nullptr);
    CHECK(iface->role() == QAccessible::Button);
    CHECK(iface->text(QAccessible::Name) == QStringLiteral("Software version, 1.4.2"));
    press_action(third);
    CHECK(activated.count() == 1);

    third->forceActiveFocus();
    rig.key(Qt::Key_Return);
    CHECK(activated.count() == 2);
}

TEST_CASE("list_row.cross_cutting: names, targets, order and text scale, in light, dark and with Reduce Motion",
          "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(load_host(rig, QString::fromUtf8(ROWS)) != nullptr);
    expect_clean(check_everything(rig));
    rig.set_dark(true);
    expect_clean(check_everything(rig));
    rig.set_reduce_motion(true);
    expect_clean(check_motion_reduced(rig));
}

TEST_CASE("list_row.focus: a tap on a row takes the focus from the text field beside it before it asks to be opened",
          "[ui][list_row]")
{
    UiRig rig;
    REQUIRE(rig.load_source(QByteArrayLiteral(R"(
        import QtQuick
        import ErrorJournalingApp
        Item {
            id: host
            property int opened: 0
            property bool fieldFocusedWhenOpened: true
            TextInput {
                id: field
                objectName: "field"
                x: 20; y: 100; width: 200; height: 40
                inputMethodHints: Qt.ImhDigitsOnly
            }
            GroupedList {
                x: Theme.space.screenMargin
                y: 200
                width: host.width - 2 * Theme.space.screenMargin
                ListRow {
                    objectName: "row"
                    text: "Environment"
                    value: "None"
                    showChevron: true
                    onActivated: {
                        host.opened += 1;
                        host.fieldFocusedWhenOpened = field.activeFocus;
                    }
                }
            }
        })")) != nullptr);
    QQuickItem* host = rig.root();
    QQuickItem* field = rig.find(QStringLiteral("field"));
    QQuickItem* row = rig.find(QStringLiteral("row"));
    REQUIRE(field != nullptr);
    REQUIRE(row != nullptr);

    field->forceActiveFocus();
    rig.show_keyboard(301);
    REQUIRE(rig.focused_input() == field);

    rig.tap(row);
    rig.advance(std::chrono::milliseconds(16));
    CHECK(host->property("opened").toInt() == 1);
    // By the time the row asks to be opened the field is no longer being typed in.
    CHECK_FALSE(host->property("fieldFocusedWhenOpened").toBool());
    CHECK(rig.focused_input() == nullptr);
    CHECK(row->hasActiveFocus());
}
