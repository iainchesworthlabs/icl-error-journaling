// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <memory>

namespace ej::test
{

/// Makes a file that exists impossible to read for as long as the object lives, the way a locked or protected file is
/// (C13): by removing its read permission on a POSIX system, by holding it open without sharing on Windows. `works()`
/// says whether that took effect; it does not when the tests run as a user for whom permissions mean nothing, and the
/// case is skipped.
class UnreadableFile
{
public:
    explicit UnreadableFile(const std::filesystem::path& path);
    ~UnreadableFile();
    UnreadableFile(const UnreadableFile&) = delete;
    UnreadableFile& operator=(const UnreadableFile&) = delete;

    [[nodiscard]] bool works() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

}  // namespace ej::test
