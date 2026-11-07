// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage_error.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;

TEST_CASE("A storage error describes itself in one line", "[storage-error]")
{
    CHECK(describe(make_error(StorageErrorKind::LOCKED, "the database file is locked", 5)) ==
          "locked: the database file is locked (code 5)");
    CHECK(describe(make_error(StorageErrorKind::NOT_FOUND, "")) == "not found");
    CHECK(describe(make_error(StorageErrorKind::DISK_FULL, "", 13)) == "disk full (code 13)");
}

TEST_CASE("Every storage error kind has a name", "[storage-error]")
{
    for (int i = 0; i <= static_cast<int>(StorageErrorKind::INTERNAL); ++i)
    {
        CHECK(storage_error_kind_name(static_cast<StorageErrorKind>(i)) != "unknown");
    }
}

TEST_CASE("Storage errors compare by value", "[storage-error]")
{
    CHECK(make_error(StorageErrorKind::IO, "x", 1) == make_error(StorageErrorKind::IO, "x", 1));
    CHECK_FALSE(make_error(StorageErrorKind::IO, "x", 1) == make_error(StorageErrorKind::IO, "x", 2));
}
