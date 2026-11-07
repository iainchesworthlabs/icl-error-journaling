// SPDX-License-Identifier: GPL-3.0-or-later
#include "rig/stage3_hooks.hpp"

#include "support/manual_storage_gateway.hpp"
#include "support/test_clock.hpp"

namespace ej::ui
{

void advance_clock(test::TestClock& clock, std::chrono::milliseconds by)
{
    clock.advance(by);
}

void run_gateway(test::ManualStorageGateway& gateway)
{
    gateway.run_all();
}

} // namespace ej::ui
