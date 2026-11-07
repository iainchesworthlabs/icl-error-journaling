// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "editors_support.hpp"

namespace ej::ui::editors
{

/// The panel of the sheet that is in the screen, or null.
[[nodiscard]] inline QQuickItem* panel(World& world)
{
    return item(world, QStringLiteral("sheetPanel"));
}

/// The item with the objectName inside the sheet.
[[nodiscard]] inline QQuickItem* in_sheet(World& world, const QString& name)
{
    return find_item(panel(world), name);
}

/// Whether the sheet is presented (from `open()` to the end of its dismissal).
[[nodiscard]] inline bool sheet_is_open(World& world)
{
    QQuickItem* panel_item = panel(world);
    return panel_item != nullptr && panel_item->parentItem()->property("isOpen").toBool();
}

/// The checks that apply to a sheet that is open over a screen: names, touch targets and the text scale at 1.0, 1.3
/// and 3.1. The reading order of the whole window is not asked of it, because the screen under a sheet is not read.
[[nodiscard]] inline Violations sheet_violations(World& world)
{
    Violations found = check_names(world.m_Ui);
    for (const double scale : {1.0, 1.3, 3.1})
    {
        world.m_Ui.set_text_scale(scale);
        world.m_Ui.layout();
        for (QString& line : check_targets(world.m_Ui))
        {
            found.push_back(std::move(line));
        }
        for (QString& line : check_text_scale(world.m_Ui))
        {
            found.push_back(QStringLiteral("at text scale %1: ").arg(scale) + line);
        }
    }
    world.m_Ui.set_text_scale(1.0);
    return found;
}

/// Taps the Cancel or Save of the sheet's bar, then lets the saves run and the sheet finish.
inline void tap_in_sheet(World& world, const QString& name)
{
    QQuickItem* target = in_sheet(world, name);
    INFO(name.toStdString());
    REQUIRE(target != nullptr);
    world.m_Ui.tap(target);
    world.settle();
    world.play(400);
    world.m_Ui.layout();
}

} // namespace ej::ui::editors
