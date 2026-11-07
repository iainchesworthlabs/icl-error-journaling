// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/ui_rig.hpp"

#include <QString>

#include <functional>
#include <vector>

namespace ej::ui
{

/// What a cross-cutting check found: one line for each fault, naming the item. Empty means the check passed.
using Violations = std::vector<QString>;

/// The cross-cutting tests of docs/plan/stage-4.md, "Tests", as functions over a loaded `UiRig`. Each component and each
/// screen test calls them on the item it loaded, in the states it can be in:
///
///     UiRig rig;
///     rig.load("Tile", {...});
///     expect_clean(check_names(rig));
///     expect_clean(check_everything(rig));
///
/// A check that cannot fail proves nothing, so the stubs in `stubs/` (`UiStubBad*`) each break one rule and the tests of
/// the rig show that the right check finds it.

/// `a11y.names`: every interactive item that is shown has a role and a name that a screen reader reaches, and no two
/// controls under one parent share a name unless their values differ. An item that is not interactive is ignored.
[[nodiscard]] Violations check_names(UiRig& rig);

/// `a11y.targets`: every interactive item that is shown is `minimum(node)` points square or more, counting the margin of
/// its pointer handlers as hit area. The minimum is 44 pt unless the test says otherwise (a tile: 88).
[[nodiscard]] Violations check_targets(UiRig& rig, const std::function<double(const Node&)>& minimum = {});

/// `a11y.reading_order`: the items a screen reader reaches are read in the order they appear, top to bottom and left to
/// right within a row.
[[nodiscard]] Violations check_reading_order(UiRig& rig);

/// `a11y.text_scale`, for the text scale the rig has now: no `Text` is cut off (`truncated`, taller or, without
/// wrapping, wider than its box) unless it elides by design, and none leaves the window sideways. A test sets the scale
/// before calling it; `check_everything` loops over 1.0, 1.3 and 3.1.
[[nodiscard]] Violations check_text_scale(UiRig& rig);

/// `a11y.safe_area`: nothing interactive or readable enters the top `top` points or the bottom `bottom` points of the
/// window, and nothing is outside the window. The caller puts the insets in the `SafeFrame` it loaded.
[[nodiscard]] Violations check_safe_area(UiRig& rig, double top, double bottom);

/// `motion.reduce`, for the state the rig has now (it sets reduceMotion first): no animation lasts more than 100 ms and
/// none slides.
[[nodiscard]] Violations check_motion_reduced(UiRig& rig);

/// The names, targets, order and text scale checks, with the text scale at 1.0, 1.3 and 3.1, restoring 1.0.
[[nodiscard]] Violations check_everything(UiRig& rig, const std::function<double(const Node&)>& minimum = {});

/// The faults, one to a line.
[[nodiscard]] QString describe(const Violations& violations);
/// A Catch2 `CHECK` that there are no faults; a failure prints them.
void expect_clean(const Violations& violations);

/// `perf.press_path`: runs `press_once` `count` times and returns the sorted times in milliseconds; `p95` is the 95th
/// percentile of them.
[[nodiscard]] std::vector<double> time_each(int count, const std::function<void()>& press_once);
[[nodiscard]] double percentile(const std::vector<double>& sorted, double fraction);

/// `perf.scene`: the number of items under `from` (the loaded item by default) and the number of those that are
/// delegates of a model, found by their class name.
struct SceneSize
{
    int m_Items = 0;
    int m_Delegates = 0;
};
[[nodiscard]] SceneSize measure_scene(UiRig& rig, QQuickItem* from = nullptr);

/// `perf.first_frame`: the rectangle of every shown item, to compare before and after the labels arrive.
struct Geometry
{
    QString m_Key; ///< the path of objectNames and indices that finds the item again
    QRectF m_Rect;
};
[[nodiscard]] std::vector<Geometry> snapshot_geometry(UiRig& rig, QQuickItem* from = nullptr);
/// The keys of the items in `after` whose rectangle differs from the one in `before`, or that only one of them has.
[[nodiscard]] std::vector<QString> moved_items(const std::vector<Geometry>& before, const std::vector<Geometry>& after);

} // namespace ej::ui
