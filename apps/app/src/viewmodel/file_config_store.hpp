// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "viewmodel/config_store.hpp"
#include "viewmodel/move_only_function.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ej::viewmodel
{

/// The production `ConfigStore`: the configuration is one JSON file (the file the person imports and exports, Stage 3
/// decision 9), replaced as a unit on every save. Created on the storage thread and used only there.
///
/// Replacing is `QSaveFile`'s: the text is written to a temporary file in the same folder, flushed, and renamed over
/// the old file, so a failure or a crash at any point leaves the old file whole. `QSaveFile` does the same on every
/// platform, so there is no platform code here (and `libs/storage` has no atomic replace to reuse: its platform layer
/// is for appending to a journal). A file that is losing a setting costs a re-import, not an entry, so this does not
/// sync the folder as the journal does.
///
/// A file that is there but is not a configuration this build reads (damaged, or written by a newer build) is never
/// lost: before `save` replaces it, the old file is copied to `<name>.invalid`, `<name>.invalid-2`, and so on. If the
/// copy cannot be made, the save fails and the old file stays.
class FileConfigStore final : public ConfigStore
{
public:
    /// Called by `save` after the new text is written to the temporary file and before it is renamed over the old file.
    /// An error cancels the save and leaves the old file; for tests, to stand for a crash between the two steps.
    using BeforeCommit = MoveOnlyFunction<std::optional<StorageError>()>;

    /// `file` is the configuration file. Its folder is made on the first save.
    explicit FileConfigStore(std::filesystem::path file);
    FileConfigStore(std::filesystem::path file, BeforeCommit before_commit);

    [[nodiscard]] Result<std::optional<std::string>, StorageError> load() override;
    Result<void, StorageError> save(std::string_view text) override;
    [[nodiscard]] Result<std::string, StorageError> read_file(std::string_view path, std::size_t max_bytes) override;
    Result<void, StorageError> write_file(std::string_view path, std::string_view text) override;

    [[nodiscard]] const std::filesystem::path& file() const
    {
        return m_File;
    }

    /// Where `save` would copy a file it finds invalid now: the first of `<name>.invalid`, `<name>.invalid-2`, ... that
    /// does not exist.
    [[nodiscard]] std::filesystem::path next_invalid_copy() const;

private:
    std::filesystem::path m_File;
    BeforeCommit m_BeforeCommit;
};

} // namespace ej::viewmodel
