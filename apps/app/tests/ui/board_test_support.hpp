// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/ui_rig.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QQuickItem>

namespace ej::ui
{

/// The item named `name` under the rig's root, which the test needs to exist: it fails the test if it does not, so a
/// caller never dereferences a null pointer (CI builds with -Werror=null-dereference).
[[nodiscard]] inline QQuickItem* require_item(UiRig& rig, const QString& name)
{
    QQuickItem* found = rig.find_item(name);
    REQUIRE(found != nullptr);
    return found;
}

/// The child item named `name` of `from`, likewise.
[[nodiscard]] inline QQuickItem* require_child(QQuickItem* from, const QString& name)
{
    REQUIRE(from != nullptr);
    QQuickItem* found = from->findChild<QQuickItem*>(name);
    REQUIRE(found != nullptr);
    return found;
}

/// What VoiceOver does on a double tap: the press action of the item's accessible interface. False when the item has no
/// interface or no press action, so a test of "reachable by VoiceOver" can fail.
[[nodiscard]] inline bool accessible_press(QQuickItem* item)
{
    QAccessibleInterface* iface = QAccessible::queryAccessibleInterface(item);
    if (iface == nullptr)
    {
        return false;
    }
    QAccessibleActionInterface* actions = iface->actionInterface();
    if (actions == nullptr || !actions->actionNames().contains(QAccessibleActionInterface::pressAction()))
    {
        return false;
    }
    actions->doAction(QAccessibleActionInterface::pressAction());
    return true;
}

} // namespace ej::ui
