// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/color_math.hpp"
#include "rig/ui_rig.hpp"

#include <QColor>
#include <QObject>
#include <QQuickItem>
#include <QString>
#include <chrono>
#include <vector>

/// What the tests of the container components (TabBar, Sheet, SheetHost, Alert, Banner, NavBar, PageStack; Stage
/// 4, 4.4) share.
namespace ej::ui::containers
{

/// Waits for an animation: the rig renders one frame at the end of each `advance`, and an animator starts on the frame
/// after it is asked to run, so a test that waits for one steps in frames of 50 ms instead of making one long wait.
inline void run(UiRig& rig, std::chrono::milliseconds total)
{
    using namespace std::chrono_literals;
    for (auto left = total; left > 0ms; left -= 50ms)
    {
        rig.advance(50ms);
    }
}

/// A colour of `Theme.colour` in the scheme the rig has now.
inline QColor theme_colour(UiRig& rig, const char* name)
{
    QObject* colours = rig.theme()->property("colour").value<QObject*>();
    return colours != nullptr ? colours->property(name).value<QColor>() : QColor();
}

/// The first node with `object_name`, or null.
inline const Node* find_node(const std::vector<Node>& nodes, const QString& object_name)
{
    for (const Node& node : nodes)
    {
        if (node.m_ObjectName == object_name)
        {
            return &node;
        }
    }
    return nullptr;
}

/// The first node with `object_name` that is shown, or null: an item that is hidden is in the tree but a screen reader
/// skips it.
inline const Node* find_shown(const std::vector<Node>& nodes, const QString& object_name)
{
    for (const Node& node : nodes)
    {
        if (node.m_ObjectName == object_name && node.m_Visible)
        {
            return &node;
        }
    }
    return nullptr;
}

/// The first item under the loaded one whose objectName is `object_name`, found through the item tree. (`UiRig::find`
/// follows the QObject tree, which does not always reach the delegates of a Repeater or a StackView.) Null when there
/// is none.
inline QQuickItem* find_item(UiRig& rig, const QString& object_name)
{
    const std::vector<Node> nodes = rig.tree();
    const Node* node = find_node(nodes, object_name);
    return node != nullptr ? node->m_Item : nullptr;
}

} // namespace ej::ui::containers
