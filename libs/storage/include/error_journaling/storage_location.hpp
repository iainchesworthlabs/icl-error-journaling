// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <filesystem>

namespace ej
{

/// Where everything of one store lives (section 2.1 of the Stage 2 plan). One directory holds the database and, beside
/// it, the folders of the journal, the backups taken before a migration and the files recovery set aside. The app gives
/// it the data directory of the sandbox; a test gives it a fresh directory.
class StorageLocation
{
public:
    explicit StorageLocation(std::filesystem::path root) : m_Root(std::move(root)) {}

    [[nodiscard]] const std::filesystem::path& root() const { return m_Root; }
    [[nodiscard]] std::filesystem::path database_path() const { return m_Root / "ej.db"; }
    [[nodiscard]] std::filesystem::path journal_dir() const { return m_Root / "journal"; }
    [[nodiscard]] std::filesystem::path archived_journal_dir() const { return m_Root / "journal" / "archive"; }
    /// The people journal (Stage 14, section 3.4): the names of the optional list of people, in a file of their own so
    /// that no log journal ever holds one.
    [[nodiscard]] std::filesystem::path people_path() const { return m_Root / "people.jsonl"; }
    [[nodiscard]] std::filesystem::path backup_dir() const { return m_Root / "backup"; }
    [[nodiscard]] std::filesystem::path quarantine_dir() const { return m_Root / "quarantine"; }
    [[nodiscard]] std::filesystem::path migration_marker() const { return m_Root / "migration.inprogress"; }

    /// Creates the root and the folders under it. Fine when they exist.
    [[nodiscard]] Result<void, StorageError> create_directories() const;

private:
    std::filesystem::path m_Root;
};

}  // namespace ej
