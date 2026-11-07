// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <chrono>

namespace ej::test
{
class TestClock;
class ManualStorageGateway;
} // namespace ej::test

namespace ej::ui
{

// The two calls that `UiRig` makes on the Stage 3 test support. They are in a file of their own because the headers of
// the core define a name (`STRICT`) that the Windows headers behind Qt Quick define as a macro, so the two must not
// meet in one translation unit.

/// Moves the wall and the monotonic clock of `clock` forward by `by` and fires the timers that fall due.
void advance_clock(test::TestClock& clock, std::chrono::milliseconds by);
/// Runs the jobs that wait in `gateway`, and their completions.
void run_gateway(test::ManualStorageGateway& gateway);

} // namespace ej::ui
