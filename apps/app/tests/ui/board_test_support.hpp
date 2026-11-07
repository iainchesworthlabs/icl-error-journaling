// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QQuickItem>

namespace ej::ui
{

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
