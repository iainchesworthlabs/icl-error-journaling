// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/storage_location.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace ej;
using namespace ej::test;

TEST_CASE("A storage location names every file and folder of the store under one root", "[storage-location]")
{
    const StorageLocation location("/data/store");
    CHECK(location.root() == std::filesystem::path("/data/store"));
    CHECK(location.database_path() == std::filesystem::path("/data/store") / "ej.db");
    CHECK(location.journal_dir() == std::filesystem::path("/data/store") / "journal");
    CHECK(location.archived_journal_dir() == std::filesystem::path("/data/store") / "journal" / "archive");
    CHECK(location.backup_dir() == std::filesystem::path("/data/store") / "backup");
    CHECK(location.quarantine_dir() == std::filesystem::path("/data/store") / "quarantine");
    CHECK(location.migration_marker() == std::filesystem::path("/data/store") / "migration.inprogress");
}

TEST_CASE("Creating the folders makes all of them, and does it again without complaint", "[storage-location]")
{
    TempDir dir;
    const StorageLocation location(dir.path() / "new" / "store");
    REQUIRE(location.create_directories().has_value());
    CHECK(std::filesystem::is_directory(location.journal_dir()));
    CHECK(std::filesystem::is_directory(location.archived_journal_dir()));
    CHECK(std::filesystem::is_directory(location.backup_dir()));
    CHECK(std::filesystem::is_directory(location.quarantine_dir()));
    CHECK(location.create_directories().has_value());
}

TEST_CASE("A folder that cannot be created is an error with a kind", "[storage-location]")
{
    TempDir dir;
    {
        std::ofstream file(dir.path() / "in-the-way");
        file << "a file where the store should be";
    }
    const StorageLocation location(dir.path() / "in-the-way" / "store");
    const auto created = location.create_directories();
    REQUIRE_FALSE(created.has_value());
    CHECK((created.error().m_Kind == StorageErrorKind::IO || created.error().m_Kind == StorageErrorKind::PERMISSION));
}
