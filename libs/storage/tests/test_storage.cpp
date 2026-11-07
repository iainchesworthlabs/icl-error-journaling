// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage.hpp"
#include "error_journaling/version.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("The application opens no store yet, and the storage library links the core library", "[storage]")
{
    CHECK(error_journaling::storage::backend_name() == "none");
    CHECK_FALSE(error_journaling::version_text().isEmpty());
}
