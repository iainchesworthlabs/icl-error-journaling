// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/ui_rig.hpp"

#include <QAccessible>
#include <QAccessibleActionInterface>
#include <QQuickItem>

namespace ej::ui
{

/// The first item at or under `from` whose objectName is `name`, found by the visual tree. `UiRig::find` follows the
/// QObject parents, which is not where a Repeater puts its delegates, so it does not find an item in one.
[[nodiscard]] inline QQuickItem* find_item(QQuickItem* from, const QString& name)
{
    if (from == nullptr)
    {
        return nullptr;
    }
    if (from->objectName() == name)
    {
        return from;
    }
    for (QQuickItem* child : from->childItems())
    {
        if (QQuickItem* found = find_item(child, name))
        {
            return found;
        }
    }
    return nullptr;
}
[[nodiscard]] inline QQuickItem* find_item(UiRig& rig, const QString& name)
{
    return find_item(rig.root(), name);
}

/// Moves the animation clock by `by` in steps of one frame, painting between them. The animators of Qt Quick take their
/// time from the frames they are painted in, so a test that waits for one moves frame by frame: a single large
/// `advance` paints once, at its end, and the animator has not seen the time go by.
inline void advance_frames(UiRig& rig, std::chrono::milliseconds by)
{
    constexpr std::chrono::milliseconds FRAME{16};
    for (std::chrono::milliseconds done{0}; done < by; done += FRAME)
    {
        rig.advance(FRAME);
    }
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
