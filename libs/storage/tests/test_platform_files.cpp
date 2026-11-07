// SPDX-License-Identifier: GPL-3.0-or-later
//
// The platform pieces on a real file system: the journal file, the durable sync, the exclusive lock (I4) and the
// directory sync. Which function does the work is chosen by CMake from src/platform/<os>/; these tests run on whichever
// that is.
#include "error_journaling/journal_file.hpp"
#include "platform/durable_sync.hpp"
#include "platform/errno_error.hpp"
#include "platform/file_lock.hpp"
#include "platform/file_protection.hpp"
#include "temp_dir.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>

using namespace ej;
using namespace ej::test;

TEST_CASE("A journal file is created, appended to, synced, read back and cut", "[platform][journal-file]")
{
    TempDir dir;
    const auto path = dir.path() / "j.jsonl";
    auto opened = open_journal_file(path);
    REQUIRE(opened.has_value());
    JournalFile& file = **opened;
    CHECK(file.size() == 0);

    REQUIRE(file.append("first\n").has_value());
    REQUIRE(file.append("second\n").has_value());
    CHECK(file.size() == 13);
    REQUIRE(file.sync().has_value());
    CHECK(file.read_all().value() == "first\nsecond\n");
    CHECK(std::filesystem::file_size(path) == 13);

    REQUIRE(file.truncate(6).has_value());
    CHECK(file.size() == 6);
    REQUIRE(file.sync().has_value());
    CHECK(file.read_all().value() == "first\n");

    REQUIRE(file.append("third\n").has_value());  // appends go to the new end
    CHECK(file.read_all().value() == "first\nthird\n");
}

TEST_CASE("An existing file is opened for appending, not overwritten, and its size is known",
          "[platform][journal-file]")
{
    TempDir dir;
    const auto path = dir.path() / "j.jsonl";
    {
        std::ofstream out(path, std::ios::binary);
        out << "old content\n";
    }
    auto opened = open_journal_file(path);
    REQUIRE(opened.has_value());
    CHECK((*opened)->size() == 12);
    REQUIRE((*opened)->append("new\n").has_value());
    CHECK((*opened)->read_all().value() == "old content\nnew\n");
}

TEST_CASE("I4: a second writer on the same file is refused, not interleaved", "[platform][journal-file][lock]")
{
    TempDir dir;
    const auto path = dir.path() / "j.jsonl";
    auto first = open_journal_file(path);
    REQUIRE(first.has_value());
    REQUIRE((*first)->append("kept\n").has_value());

    const auto second = open_journal_file(path);
    REQUIRE_FALSE(second.has_value());
    CHECK(second.error().m_Kind == StorageErrorKind::LOCKED);

    // The lock stops writers only: the file can still be read while it is held.
    std::ifstream in(path, std::ios::binary);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    CHECK(content == "kept\n");

    first->reset();  // closing the file gives the lock up
    CHECK(open_journal_file(path).has_value());
}

TEST_CASE("A file that cannot be opened is an error with a kind", "[platform][journal-file]")
{
    TempDir dir;
    const auto in_missing_folder = open_journal_file(dir.path() / "no-such-folder" / "j.jsonl");
    REQUIRE_FALSE(in_missing_folder.has_value());
    CHECK((in_missing_folder.error().m_Kind == StorageErrorKind::IO ||
           in_missing_folder.error().m_Kind == StorageErrorKind::PERMISSION));
}

TEST_CASE("The directory sync works on a folder and reports one that is not there", "[platform]")
{
    TempDir dir;
    CHECK(platform::sync_directory(dir.path()).has_value());
    // Best effort on Windows, an error elsewhere: either way, no crash.
    (void)platform::sync_directory(dir.path() / "missing");
}

TEST_CASE("The file protection is a no-op on a desktop and succeeds", "[platform]")
{
    TempDir dir;
    CHECK(platform::apply_file_protection(dir.path()).has_value());
}

TEST_CASE("The errno mapping tells a full disk and a refusal from other failures", "[platform]")
{
    CHECK(platform::error_from_errno("x", ENOSPC).m_Kind == StorageErrorKind::DISK_FULL);
    CHECK(platform::error_from_errno("x", EACCES).m_Kind == StorageErrorKind::PERMISSION);
    CHECK(platform::error_from_errno("x", EPERM).m_Kind == StorageErrorKind::PERMISSION);
    CHECK(platform::error_from_errno("x", EROFS).m_Kind == StorageErrorKind::PERMISSION);
    CHECK(platform::error_from_errno("x", EIO).m_Kind == StorageErrorKind::IO);
    CHECK(platform::error_from_errno("x", ENOSPC).m_NativeCode == ENOSPC);
}

TEST_CASE("An empty path cannot be opened for appending", "[platform]")
{
    CHECK_FALSE(platform::open_for_append(std::filesystem::path()).has_value());
}
