// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/clock.hpp"
#include "error_journaling/result.hpp"
#include "error_journaling/storage_error.hpp"
#include "error_journaling/storage_location.hpp"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ej
{

/// Where recovery puts what it replaces (section 2.7 step 6). One recovery run uses one folder,
/// `quarantine/<UTC time>-<n>/`, made the first time it is needed. The time is the clock's, and the counter makes the
/// name new even when two runs fall in the same second or the clock stepped back, so a name is never reused and nothing
/// is overwritten. Files are moved in whole; the app never deletes one. `prune` keeps the newest few folders.
class Quarantine
{
public:
    Quarantine(const StorageLocation& location, const Clock& clock) : m_Location(location), m_Clock(clock) {}

    /// Moves the file into this run's folder under its own name (or, when that name is taken, with a number added) and
    /// returns where it went. Nothing is moved if the file is not there.
    [[nodiscard]] Result<std::filesystem::path, StorageError> take(const std::filesystem::path& file);

    /// Copies the file into this run's folder and leaves it where it is. For a file that is about to be replaced by a
    /// rename, so that there is never a moment without it.
    [[nodiscard]] Result<std::filesystem::path, StorageError> take_copy(const std::filesystem::path& file);

    /// The paths of the files taken so far, relative to the store, for the report.
    [[nodiscard]] const std::vector<std::string>& taken() const { return m_Taken; }

    /// Removes the oldest quarantine folders beyond the newest `keep`. Run once, at the end of a recovery that took
    /// something.
    [[nodiscard]] Result<void, StorageError> prune(std::size_t keep) const;

    /// The name a folder would get for this time and counter: `20261107T043000Z-1`. Public for the tests.
    [[nodiscard]] static std::string folder_name(UtcMillis time, unsigned counter);

private:
    [[nodiscard]] Result<std::filesystem::path, StorageError> folder();

    StorageLocation m_Location;
    const Clock& m_Clock;
    std::filesystem::path m_Folder;
    std::vector<std::string> m_Taken;
};

}  // namespace ej
