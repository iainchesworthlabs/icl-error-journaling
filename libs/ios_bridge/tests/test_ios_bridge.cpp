// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "error_journaling/ios_bridge.hpp"

TEST_CASE("The bridge reports the implementation CMake linked", "[ios_bridge]")
{
    // The tests run on a desktop, so the stub is the one linked.
    CHECK(error_journaling::bridge::implementation_name() == "desktop-stub");
}
