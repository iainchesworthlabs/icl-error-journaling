// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "icl_template/greeting.hpp"

TEST_CASE("Greeter greets by name", "[greeting]")
{
    const icl_template::Greeter greeter("Ada");
    const auto text = greeter.greet();
    CHECK(text.starts_with("Hello, Ada, from "));
    CHECK(text.ends_with("."));
    CHECK(greeter.name() == "Ada");
}

TEST_CASE("Greeter accepts an empty name", "[greeting]")
{
    const icl_template::Greeter greeter("");
    CHECK(greeter.greet().starts_with("Hello, , from "));
}
