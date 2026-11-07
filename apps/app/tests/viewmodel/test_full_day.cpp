// SPDX-License-Identifier: GPL-3.0-or-later
//
// The exit-gate test of Stage 3, in memory: a full day through the services, headless (docs/plan/stage-3.md). The same
// day against the real store is in integration/test_full_day.cpp.
#include "support/full_day.hpp"
#include "support/rig.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A full day, headless, on the in-memory store", "[viewmodel][full_day][exit_gate]")
{
    ej::test::Rig rig(ej::test::RigStore::MEMORY);
    ej::test::full_day(rig);
}
