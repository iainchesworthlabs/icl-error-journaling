// SPDX-License-Identifier: GPL-3.0-or-later
//
// The exit-gate test of Stage 3 against the real store: the SQLite repository over the file journal and the
// configuration file in a temporary folder, on a real storage thread, with recovery at every start
// (docs/plan/stage-3.md, "The exit-gate test: a full day, headless"). It is the same day as test_full_day.cpp.
#include "support/full_day.hpp"
#include "support/rig.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("A full day, headless, on the real store and a storage thread", "[viewmodel][full_day][exit_gate]")
{
    ej::test::Rig rig(ej::test::RigStore::SQLITE);
    ej::test::full_day(rig);
}
