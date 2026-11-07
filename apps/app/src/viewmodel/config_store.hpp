// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace ej::viewmodel
{

/// Where the configuration file (the same JSON the person imports and exports, Stage 3 decision 9) is kept, and the
/// files they pick for import and export. It lives beside the `LogRepository` on the storage thread and is used only
/// there. It deals in text: `import_configuration` and `export_configuration` stay in the service that calls it, so a
/// fake needs no JSON.
///
/// 3.1 declares the interface because `StorageContext` refers to it. `FileConfigStore` and the service that uses it
/// are work package 3.3's.
class ConfigStore
{
public:
    ConfigStore() = default;
    ConfigStore(const ConfigStore&) = delete;
    ConfigStore& operator=(const ConfigStore&) = delete;
    virtual ~ConfigStore() = default;

    /// The saved configuration file, or nothing when there is none yet.
    [[nodiscard]] virtual Result<std::optional<std::string>, StorageError> load() = 0;

    /// Replaces the saved configuration file as one unit: after a failure the old file is still there.
    virtual Result<void, StorageError> save(std::string_view text) = 0;

    /// Reads a file the person chose, up to `max_bytes`; a larger file is refused with `LIMIT_EXCEEDED`.
    [[nodiscard]] virtual Result<std::string, StorageError> read_file(std::string_view path, std::size_t max_bytes) = 0;

    virtual Result<void, StorageError> write_file(std::string_view path, std::string_view text) = 0;
};

} // namespace ej::viewmodel
