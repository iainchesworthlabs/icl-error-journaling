// SPDX-License-Identifier: GPL-3.0-or-later
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "error_journaling/version.hpp"

TEST_CASE("The core library reports the project version", "[core]")
{
    CHECK_FALSE(error_journaling::version_text().isEmpty());
    CHECK(error_journaling::version_text().toStdString() == std::string(error_journaling::VERSION_STRING));
}
