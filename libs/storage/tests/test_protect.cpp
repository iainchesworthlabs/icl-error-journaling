// SPDX-License-Identifier: GPL-3.0-or-later
//
// File protection (work package 5.4 of docs/plan/stage-5.md): every file and folder the store creates is handed to the
// protection setter, and a setter that fails never stops the store from opening or working. The setter is a fake that
// records its calls, so this runs on every platform; the iOS call itself is checked on a device. All data is synthetic.
#include "platform/android_protection.hpp"
#include "protect.hpp"
#include "recovery_fixture.hpp"

#include <catch2/catch_test_macros.hpp>

#include <set>

using namespace ej;
using namespace ej::test;

namespace
{

using platform::ProtectionClass;

class RecordingProtection final : public platform::FileProtection
{
public:
    bool m_FailApply = false;
    bool m_FailReadBack = false;
    std::set<std::filesystem::path> m_Applied;
    std::set<std::filesystem::path> m_ReadBack;
    std::vector<std::filesystem::path> m_Calls;  // in order, with repeats

    Result<void, StorageError> apply(const std::filesystem::path& path, ProtectionClass wanted) override
    {
        if (m_FailApply)
        {
            return fail(make_error(StorageErrorKind::IO, "the fake refused", 7));
        }
        CHECK(wanted == ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION);
        m_Applied.insert(path.lexically_normal());
        m_Calls.push_back(path.lexically_normal());
        return {};
    }

    Result<ProtectionClass, StorageError> read_back(const std::filesystem::path& path) override
    {
        m_ReadBack.insert(path.lexically_normal());
        if (m_FailReadBack)
        {
            return fail(make_error(StorageErrorKind::IO, "the fake could not read", 8));
        }
        return m_Applied.contains(path.lexically_normal()) ? ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION
                                                           : ProtectionClass::NONE;
    }
};

/// The paths under `root` (and `root` itself) that the fake was not asked to protect.
std::vector<std::string> unprotected(const std::filesystem::path& root, const RecordingProtection& fake)
{
    std::vector<std::string> missing;
    if (!fake.m_Applied.contains(root.lexically_normal()))
    {
        missing.push_back(".");
    }
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
    {
        if (!fake.m_Applied.contains(entry.path().lexically_normal()))
        {
            missing.push_back(std::filesystem::relative(entry.path(), root).generic_string());
        }
    }
    return missing;
}

void use_store(OpenStore& store)
{
    HeaderValues header = sample_header();
    header.m_LocalDate = std::chrono::year{2026} / 11 / 5;
    REQUIRE(store.repo().create_log(log_id(1), header).has_value());
    REQUIRE(store.repo().add_entry(log_id(1), mixed_entry(1, 10)).has_value());
    header.m_LocalDate = std::chrono::year{2026} / 11 / 6;
    REQUIRE(store.repo().start_new_log(log_id(1), log_id(2), header).has_value());
    REQUIRE(store.repo().add_entry(log_id(2), mixed_entry(2, 20)).has_value());
    REQUIRE(store.repo().set_draft("half a note", sample_time(30)).has_value());
}

}  // namespace

TEST_CASE("Every folder and file a working store creates is protected, and read back", "[protect]")
{
    RecordingProtection fake;
    const ScopedFileProtection scope(fake);
    TempDir dir;
    const auto root = dir.path() / "store";
    {
        OpenStore store(root);
        use_store(store);
        // The database's -wal and -shm files exist while it is open.
        CHECK(unprotected(root, fake) == std::vector<std::string>{});
    }
    CHECK(unprotected(root, fake) == std::vector<std::string>{});
    for (const auto& path : fake.m_Applied)
    {
        CHECK(fake.m_ReadBack.contains(path));
        // Nothing outside the store is touched.
        CHECK(path.lexically_normal().generic_string().starts_with(root.lexically_normal().generic_string()));
    }
}

TEST_CASE("A store made before protection existed is swept once, and the next launch does not sweep", "[protect]")
{
    TempDir dir;
    const auto root = dir.path() / "store";
    {
        OpenStore store(root);
        use_store(store);
    }
    {
        RecordingProtection fake;
        const ScopedFileProtection scope(fake);
        REQUIRE(recover(StorageLocation(root)).has_value());
        CHECK(unprotected(root, fake) == std::vector<std::string>{});
        CHECK(fake.m_Calls.back() == root.lexically_normal());  // the folder, last
    }
    {
        // The second launch: the folder reads back as protected, so only the folders, the database and the open
        // journals are touched, not the archive.
        RecordingProtection fake;
        fake.m_Applied.insert(root.lexically_normal());
        const ScopedFileProtection scope(fake);
        REQUIRE(recover(StorageLocation(root)).has_value());
        CHECK_FALSE(fake.m_Applied.contains((root / "journal" / "archive" /
                                             "00000000-0000-4000-8000-000000000001.jsonl").lexically_normal()));
    }
}

TEST_CASE("A store that is rebuilt protects the quarantine and the rebuilt database", "[protect]")
{
    TempDir dir;
    const auto root = dir.path() / "store";
    {
        OpenStore store(root);
        use_store(store);
    }
    write_bytes(root / "ej.db", std::string(4096, 'x'));  // not a database any more

    RecordingProtection fake;
    const ScopedFileProtection scope(fake);
    const auto report = recover(StorageLocation(root));
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::REBUILT);
    CHECK_FALSE(report->m_QuarantinedPaths.empty());
    CHECK(unprotected(root, fake) == std::vector<std::string>{});
}

TEST_CASE("A setter that fails neither blocks the store nor loses an entry, and leaves warnings", "[protect]")
{
    RecordingProtection fake;
    fake.m_FailApply = true;
    const ScopedFileProtection scope(fake);
    const auto before = protection_warnings().size();
    TempDir dir;
    const auto root = dir.path() / "store";
    {
        OpenStore store(root);
        use_store(store);
        const auto log = store.repo().load_log(log_id(2));
        REQUIRE(log.has_value());
        CHECK(digest_of(*log).m_Entries == 1);
    }
    CHECK(protection_warnings().size() > before);
    for (const auto& line : protection_warnings())
    {
        CHECK(line.starts_with("protection: "));
    }
}

TEST_CASE("A read back that fails is a warning too", "[protect]")
{
    RecordingProtection fake;
    fake.m_FailReadBack = true;
    const ScopedFileProtection scope(fake);
    const auto before = protection_warnings().size();
    TempDir dir;
    {
        OpenStore store(dir.path() / "store");
        use_store(store);
    }
    CHECK(protection_warnings().size() > before);
}

TEST_CASE("A path that is not there is skipped without a warning", "[protect]")
{
    RecordingProtection fake;
    const ScopedFileProtection scope(fake);
    const auto before = protection_warnings().size();
    TempDir dir;
    protect_path(dir.path() / "missing");
    CHECK(fake.m_Applied.empty());
    CHECK(protection_warnings().size() == before);
}

TEST_CASE("The desktop stub applies nothing and reads back UNSUPPORTED", "[protect]")
{
    TempDir dir;
    auto& system = platform::system_file_protection();
    CHECK(system.apply(dir.path(), ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION).has_value());
    const auto found = system.read_back(dir.path());
    REQUIRE(found.has_value());
    CHECK(*found == ProtectionClass::UNSUPPORTED);
}

TEST_CASE("Android: the credential-encrypted storage of an app is complete-until-first-unlock", "[protect][android]")
{
    using platform::ProtectionClass;
    const auto cls = [](const char* path) { return platform::android_protection_class(path); };
    constexpr auto first_unlock = ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION;

    // Where the store (files/) and the share and import copies (cache/) are, under both names Android gives the path.
    CHECK(cls("/data/user/0/com.example.app/files") == first_unlock);
    CHECK(cls("/data/user/0/com.example.app/files/ej.db") == first_unlock);
    CHECK(cls("/data/user/0/com.example.app/files/journal/archive/2026-11-09.jsonl") == first_unlock);
    CHECK(cls("/data/user/0/com.example.app/cache/ej-share/setup.ejconfig") == first_unlock);
    CHECK(cls("/data/user/10/com.example.app/files/ej.db") == first_unlock); // a secondary user
    CHECK(cls("/data/data/com.example.app/files/ej.db") == first_unlock);
    CHECK(cls("/data/user/0/com.example.app/files/./journal//x") == first_unlock); // spelled oddly, normalised
}

TEST_CASE("Android: device-encrypted storage is NONE and everything else is OTHER", "[protect][android]")
{
    using platform::ProtectionClass;
    const auto cls = [](const char* path) { return platform::android_protection_class(path); };

    // Readable before the first unlock: the application does not use it.
    CHECK(cls("/data/user_de/0/com.example.app/files/x") == ProtectionClass::NONE);

    CHECK(cls("/storage/emulated/0/Download/setup.ejconfig") == ProtectionClass::OTHER);
    CHECK(cls("/sdcard/setup.ejconfig") == ProtectionClass::OTHER);
    CHECK(cls("/tmp/x") == ProtectionClass::OTHER);
    CHECK(cls("files/ej.db") == ProtectionClass::OTHER); // relative
    CHECK(cls("") == ProtectionClass::OTHER);
    CHECK(cls("/") == ProtectionClass::OTHER);
    // The package itself and the bare prefixes are not a file inside it.
    CHECK(cls("/data/user/0/com.example.app") == ProtectionClass::OTHER);
    CHECK(cls("/data/user/0") == ProtectionClass::OTHER);
    CHECK(cls("/data/user") == ProtectionClass::OTHER);
    CHECK(cls("/data/data/com.example.app") == ProtectionClass::OTHER);
    // A user number must be a number.
    CHECK(cls("/data/user/abc/com.example.app/files/x") == ProtectionClass::OTHER);
    CHECK(cls("/data/user//com.example.app/files/x") == ProtectionClass::OTHER);
}

TEST_CASE("Android: a path that climbs out of the app's storage is not trusted", "[protect][android]")
{
    using platform::ProtectionClass;
    const auto cls = [](const char* path) { return platform::android_protection_class(path); };
    // Four ".." from files/ land in /data, which is no app's storage.
    CHECK(cls("/data/user/0/com.example.app/files/../../../../sdcard/x") == ProtectionClass::OTHER);
    // One that stays inside normalises to a place that is inside.
    CHECK(cls("/data/user/0/com.example.app/files/../cache/x") ==
          ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION);
    // "/.." is "/" to the file system as well, so this is the same place. A name that merely contains ".." is refused: the
    // answer for an odd spelling is the cautious one.
    CHECK(cls("/../data/user/0/com.example.app/files/x") == ProtectionClass::COMPLETE_UNTIL_FIRST_USER_AUTHENTICATION);
    CHECK(cls("/data/user/0/com.example.app/files/a..b") == ProtectionClass::OTHER);
}
