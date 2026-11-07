// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "support/rig.hpp"

namespace ej::test
{

/// The exit-gate test of Stage 3: a full day, headless (docs/plan/stage-3.md, "The exit-gate test: a full day,
/// headless"). It drives the day through the public interfaces of the services only, the invokables and properties QML
/// would use: header, taps, notes, a version change, review edits, share, the new day and History, and compares the
/// text three times with `libs/core/tests/golden/prd-example.txt`.
///
/// It is written once and run twice: `unit` on `RigStore::MEMORY` and `integration` on `RigStore::SQLITE`. The rig must
/// be new (the clock at its start, an empty store). Call it from inside a Catch2 test case.
void full_day(Rig& rig);

} // namespace ej::test
