// SPDX-License-Identifier: GPL-3.0-or-later
//
// The optional commit of a software version (HDR-09) in the real store: the database column and the journal carry the
// stored text "1.4.2+a1b2c3d", and everything that older builds wrote (the three components alone) still reads, with no
// commit. Nothing in the schema changed, so a database from an older build needs no migration, and a log written before
// commits existed loads exactly as it did.
#include "recovery_fixture.hpp"

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 1;

HeaderValues header_with(const char* version, const char* commit)
{
    HeaderValues header = sample_header();
    header.m_SoftwareVersion = SoftwareVersion::parse(version, commit).value();
    return header;
}

std::string version_in_database(const std::filesystem::path& database)
{
    RawDb raw(database);
    return raw.scalar(QStringLiteral("SELECT software_version FROM day_log")).toString().toStdString();
}

} // namespace

TEST_CASE("A version without a commit is stored as the three components, as older builds stored it", "[commit][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
    }
    CHECK(version_in_database(dir.path() / "ej.db") == "1.4.2");
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    CHECK(journal.find("\"version\":\"1.4.2\"") != std::string::npos);
}

TEST_CASE("A database row written by an older build reads as a version without a commit", "[commit][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("1.9.9", "")).has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(LOG), sample_change(1, 10, HeaderField::SOFTWARE_VERSION, "1.9.10"))
                    .has_value());
    }
    {
        // The row as an older build left it: the three components alone, in the same columns. Written here by hand so
        // that the test does not depend on what this build writes.
        RawDb raw(dir.path() / "ej.db");
        REQUIRE(raw.exec(QStringLiteral("UPDATE day_log SET software_version = '1.4.2'")));
        REQUIRE(raw.exec(QStringLiteral("UPDATE header_change_entry SET old_value = '1.4.2', new_value = '1.5.0'")));
    }
    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    const HeaderValues& header = open->m_Log.initial_header();
    REQUIRE(header.m_SoftwareVersion.has_value());
    CHECK(header.m_SoftwareVersion->format() == "1.4.2");
    CHECK(header.m_SoftwareVersion->commit().empty());
    CHECK(header.m_SoftwareVersion->display() == "v1.4.2");
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "1.5.0");

    const auto logs = store.repo().list_logs().value();
    REQUIRE(logs.size() == 1);
    CHECK(logs[0].m_Header.m_SoftwareVersion == SoftwareVersion::parse("1.4.2").value());
}

TEST_CASE("The commit of a version is stored in the database and the journal and reads back", "[commit][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("1.4.2", "0123456")).has_value());
        // A correction can change the commit of the version the log started with, and take it away.
        REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::SOFTWARE_VERSION, "1.4.2").has_value());
        REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::SOFTWARE_VERSION, "1.4.2+a1b2c3d").has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(LOG), sample_change(1, 10, HeaderField::SOFTWARE_VERSION, "1.5.0+e4f5a6b"))
                    .has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(LOG), sample_change(2, 20, HeaderField::SOFTWARE_VERSION, "1.5.0"))
                    .has_value());
    }
    CHECK(version_in_database(dir.path() / "ej.db") == "1.4.2+a1b2c3d");
    {
        RawDb raw(dir.path() / "ej.db");
        CHECK(raw.scalar(QStringLiteral("SELECT old_value FROM header_change_entry WHERE new_value = '1.5.0+e4f5a6b'"))
                  .toString() == "1.4.2+a1b2c3d");
        CHECK(raw.scalar(QStringLiteral("SELECT old_value FROM header_change_entry WHERE new_value = '1.5.0'"))
                  .toString() == "1.5.0+e4f5a6b");
    }
    // The create record keeps the version the log started with; the corrections are records of their own.
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    CHECK(journal.find("\"version\":\"1.4.2+0123456\"") != std::string::npos);
    CHECK(journal.find("1.4.2+a1b2c3d") != std::string::npos);

    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_SoftwareVersion == SoftwareVersion::parse("1.4.2", "a1b2c3d").value());
    CHECK(store.repo().list_logs().value().at(0).m_Header.m_SoftwareVersion->commit() == "a1b2c3d");
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "1.5.0+e4f5a6b");
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(2))->m_Body).m_NewValue == "1.5.0");
}

TEST_CASE("The commit survives a rebuild of the database from the journal", "[commit][recovery]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("1.4.2", "a1b2c3d")).has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(LOG), sample_change(1, 10, HeaderField::SOFTWARE_VERSION, "1.5.0+e4f5a6b"))
                    .has_value());
        // With a change entry in the log, a correction fixes that entry (the value in force), not the start.
        REQUIRE(
            store.repo().correct_header(log_id(LOG), HeaderField::SOFTWARE_VERSION, "1.5.0+0123456789ab").has_value());
    }
    std::filesystem::remove(dir.path() / "ej.db");
    const auto report = recover(StorageLocation(dir.path()));
    const std::string why = report ? std::string() : describe(report.error());
    INFO(why);
    REQUIRE(report.has_value());
    CHECK(report->m_Outcome == RecoveryOutcome::REBUILT);

    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_SoftwareVersion == SoftwareVersion::parse("1.4.2", "a1b2c3d").value());
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "1.5.0+0123456789ab");
    CHECK(open->m_Log.entries().size() == 1);
}

TEST_CASE("The journal of an older build, with the three components alone, rebuilds the database", "[commit][recovery]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
        REQUIRE(store.repo()
                    .add_entry(log_id(LOG), sample_change(1, 10, HeaderField::SOFTWARE_VERSION, "1.5.0"))
                    .has_value());
    }
    // This build wrote the three components alone for a version without a commit, as the build before it did.
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    REQUIRE(journal.find("\"version\":\"1.4.2\"") != std::string::npos);
    REQUIRE(journal.find('+') == std::string::npos);
    std::filesystem::remove(dir.path() / "ej.db");
    REQUIRE(recover(StorageLocation(dir.path())).has_value());

    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_SoftwareVersion == SoftwareVersion::parse("1.4.2").value());
    CHECK(open->m_Log.initial_header().m_SoftwareVersion->commit().empty());
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "1.5.0");
}

TEST_CASE("A stored version with a damaged commit is reported as corruption, not read as something else",
          "[commit][sqlite][damage]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("1.4.2", "a1b2c3d")).has_value());
    }
    for (const char* damaged :
         {"1.4.2+", "1.4.2+abc", "1.4.2+g1b2c3d", "1.4.2+a1b2c3d4e5f6a", "+a1b2c3d", "1.4+a1b2c3d"})
    {
        INFO(damaged);
        TempDir copy_dir;
        std::filesystem::copy_file(dir.path() / "ej.db", copy_dir.path() / "ej.db");
        {
            RawDb raw(copy_dir.path() / "ej.db");
            REQUIRE(
                raw.exec(QStringLiteral("UPDATE day_log SET software_version = ?"), {QString::fromLatin1(damaged)}));
        }
        ScriptedClock clock;
        NullJournal journal;
        auto repo = SqliteLogRepository::open(copy_dir.path() / "ej.db", clock, journal);
        REQUIRE(repo.has_value());
        const auto loaded = (*repo)->load_log(log_id(LOG));
        REQUIRE_FALSE(loaded.has_value());
        CHECK(loaded.error().m_Kind == StorageErrorKind::CORRUPT);
    }
}
