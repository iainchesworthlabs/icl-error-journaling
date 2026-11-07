// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/color_math.hpp"
#include "rig/ui_rig.hpp"

#include <QColor>
#include <QObject>
#include <QQuickItem>
#include <QString>
#include <vector>

/// What the tests of the container components (TabBar, Sheet, SheetHost, Alert, Banner, NavBar, PageStack; Stage
/// 4, 4.4) share.
namespace ej::ui::containers
{

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

} // namespace ej::ui::containers
