// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/ui_checks.hpp"
#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QByteArray>
#include <QColor>
#include <QObject>
#include <QQuickItem>
#include <QString>
#include <algorithm>

/// What the tests of the input and list components of package 4.6 share: a host item for the component under test (the
/// rig sizes the item it loads to the window, which would hide the component's own size), and a few lookups.
namespace ej::ui::input
{

/// Loads an item whose children are `body`, a QML snippet that may use the module, and which is `host` to the snippet.
[[nodiscard]] inline QQuickItem* load_host(UiRig& rig, const QString& body)
{
    const QByteArray source =
        QByteArrayLiteral("import QtQuick\nimport QtQuick.Controls.Basic\nimport ErrorJournalingApp\nItem {\n"
                          "    id: host\n") +
        body.toUtf8() + QByteArrayLiteral("\n}\n");
    return rig.load_source(source);
}

/// A lookup that must find its item: the test stops with a failure when there is none, so the pointer is never null
/// after it.
[[nodiscard]] inline QQuickItem* require_item(UiRig& rig, const QString& name, QQuickItem* from = nullptr)
{
    QQuickItem* item = rig.find(name, from);
    REQUIRE(item != nullptr);
    return item;
}

/// The loaded item, which the test has loaded and which therefore exists.
[[nodiscard]] inline QQuickItem* require_root(UiRig& rig)
{
    QQuickItem* root = rig.root();
    REQUIRE(root != nullptr);
    return root;
}

/// The object that a property of `owner` holds (the `border` of a rectangle, the `background` of a field).
[[nodiscard]] inline QObject* require_object(const QObject* owner, const char* property)
{
    REQUIRE(owner != nullptr);
    QObject* object = owner->property(property).value<QObject*>();
    REQUIRE(object != nullptr);
    return object;
}

/// The accessibility interface of an item, which every item of the components has.
[[nodiscard]] inline QAccessibleInterface* require_interface(QQuickItem* item)
{
    REQUIRE(item != nullptr);
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item);
    REQUIRE(iface != nullptr);
    return iface;
}

/// What VoiceOver does to activate an item: its accessibility press action.
inline void press_action(QQuickItem* item)
{
    QAccessibleActionInterface* actions = require_interface(item)->actionInterface();
    REQUIRE(actions != nullptr);
    actions->doAction(QAccessibleActionInterface::pressAction());
}

/// `Theme.colour.<name>` of this rig's engine, for the scheme the rig is in.
[[nodiscard]] inline QColor theme_colour(UiRig& rig, const char* name)
{
    const QObject* theme = rig.theme();
    REQUIRE(theme != nullptr);
    const QObject* colours = require_object(theme, "colour");
    return colours->property(name).value<QColor>();
}

/// The first child of `item` that is a `Rectangle`, or null: the fill that the components draw first.
[[nodiscard]] inline QQuickItem* first_rectangle(QQuickItem* item)
{
    for (QQuickItem* child : item->childItems())
    {
        if (QByteArray(child->metaObject()->className()).startsWith("QQuickRectangle"))
        {
            return child;
        }
    }
    return nullptr;
}

/// The colour of the first `Rectangle` child of `item`, or an invalid colour.
[[nodiscard]] inline QColor fill_of(QQuickItem* item)
{
    const QQuickItem* rectangle = first_rectangle(item);
    return rectangle != nullptr ? rectangle->property("color").value<QColor>() : QColor();
}

/// The border width of the first `Rectangle` child of `item`, or -1.
[[nodiscard]] inline double border_of(QQuickItem* item)
{
    const QQuickItem* rectangle = first_rectangle(item);
    return rectangle != nullptr ? require_object(rectangle, "border")->property("width").toDouble() : -1.0;
}

/// True when a line of `violations` has `text` in it.
[[nodiscard]] inline bool any_line_has(const Violations& violations, const QString& text)
{
    return std::ranges::any_of(violations, [&text](const QString& line) { return line.contains(text); });
}

} // namespace ej::ui::input
