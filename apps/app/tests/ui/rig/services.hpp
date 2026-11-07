// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "rig/ui_rig.hpp"
#include "support/rig.hpp"

#include <QString>
#include <QVariantMap>

namespace ej::ui
{

/// Puts the `Rig` of Stage 3 (the whole application, headless, over the in-memory store and the fake clock and
/// platform) under a `UiRig`, so that a screen test uses the real view models and sees the state between "submitted"
/// and "committed":
///
///     test::Rig services;                       // RigStore::MEMORY: nothing runs until settle()
///     services.import_sample_configuration();   // Sam Example, UNIT-27, North Site and the sample board
///     UiRig rig;
///     use(rig, services);
///     rig.load("LogScreen", screen_properties(services));
///     services.tap(type_id(services, "Mute Delay"));    // the haptic, then recordError: nothing has been committed
///     rig.settle();                                     // the commit, then the toast, the count and the Last bar
///     rig.advance(5s);                                  // the undo window ends
///
/// After this, `UiRig::advance` moves the clocks of the services (the wall clock, the monotonic clock, the timers) and
/// `settle` runs the store until it is idle. The services must outlive the use of them, and `Rig::restart()` makes new
/// ones, so load the screen again after it.
void use(UiRig& rig, test::Rig& services);

/// The models as the initial properties of a screen, under the names of the plan (`session`, `undo`, `board`, `picker`,
/// `timeline`, `history`, `day`, `settings`, `share`), plus whatever is in `more` (a screen takes `bridge` and the rest
/// from the test, which makes it).
[[nodiscard]] QVariantMap screen_properties(test::Rig& services, const QVariantMap& more = {});

/// The `typeId` role of the board row whose label is `label` ("Mute Delay"); empty when there is none.
[[nodiscard]] QString type_id(test::Rig& services, const QString& label);

/// The `count` role of the board row whose label is `label`; -1 when there is none.
[[nodiscard]] int board_count(test::Rig& services, const QString& label);

} // namespace ej::ui
