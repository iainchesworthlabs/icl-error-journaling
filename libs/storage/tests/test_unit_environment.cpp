// SPDX-License-Identifier: GPL-3.0-or-later
//
// The optional environment of a unit (PRF-11) in the real store: the database column and the journal carry the stored
// text "27-prod", and everything that older builds wrote (the digits alone) still reads, with no environment. Nothing
// in the schema changed, so a database from an older build needs no migration.
#include "recovery_fixture.hpp"

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 1;

HeaderValues header_with(const char* digits, const char* environment)
{
    HeaderValues header = sample_header();
    header.m_UnitNumber = UnitNumber::parse(digits, environment).value();
    return header;
}

std::string unit_in_database(const std::filesystem::path& database)
{
    RawDb raw(database);
    return raw.scalar(QStringLiteral("SELECT unit_number FROM day_log")).toString().toStdString();
}

} // namespace

TEST_CASE("A unit without an environment is stored as the digits, as older builds stored it", "[environment][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
    }
    CHECK(unit_in_database(dir.path() / "ej.db") == "27");
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    CHECK(journal.find("\"unit\":\"27\"") != std::string::npos);
}

TEST_CASE("A database row written by an older build reads as a unit without an environment", "[environment][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("31", "")).has_value());
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(1, 10, HeaderField::UNIT_NUMBER, "32")).has_value());
    }
    {
        // The row as an older build left it: digits only, in the same columns. Written here by hand so that the test
        // does not depend on what this build writes.
        RawDb raw(dir.path() / "ej.db");
        REQUIRE(raw.exec(QStringLiteral("UPDATE day_log SET unit_number = '27'")));
        REQUIRE(raw.exec(QStringLiteral("UPDATE header_change_entry SET old_value = '27', new_value = '28'")));
    }
    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    const HeaderValues& header = open->m_Log.initial_header();
    REQUIRE(header.m_UnitNumber.has_value());
    CHECK(header.m_UnitNumber->digits() == "27");
    CHECK(header.m_UnitNumber->environment().empty());
    CHECK(header.m_UnitNumber->format("UNIT") == "UNIT-27");
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "28");

    const auto logs = store.repo().list_logs().value();
    REQUIRE(logs.size() == 1);
    CHECK(logs[0].m_Header.m_UnitNumber == UnitNumber::parse("27").value());
}

TEST_CASE("The environment of a unit is stored in the database and the journal and reads back", "[environment][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("27", "uat")).has_value());
        // A correction can change the environment of the unit the log started with, and take it away.
        REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::UNIT_NUMBER, "27").has_value());
        REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::UNIT_NUMBER, "27-prod").has_value());
        REQUIRE(
            store.repo().add_entry(log_id(LOG), sample_change(1, 10, HeaderField::UNIT_NUMBER, "27-dev")).has_value());
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(2, 20, HeaderField::UNIT_NUMBER, "28")).has_value());
    }
    CHECK(unit_in_database(dir.path() / "ej.db") == "27-prod");
    {
        RawDb raw(dir.path() / "ej.db");
        CHECK(raw.scalar(QStringLiteral("SELECT old_value FROM header_change_entry WHERE new_value = '27-dev'"))
                  .toString() == "27-prod");
        CHECK(
            raw.scalar(QStringLiteral("SELECT old_value FROM header_change_entry WHERE new_value = '28'")).toString() ==
            "27-dev");
    }
    // The create record keeps the unit the log started with; the corrections are records of their own.
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    CHECK(journal.find("\"unit\":\"27-uat\"") != std::string::npos);
    CHECK(journal.find("27-prod") != std::string::npos);

    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_UnitNumber == UnitNumber::parse("27", "prod").value());
    CHECK(store.repo().list_logs().value().at(0).m_Header.m_UnitNumber->environment() == "prod");
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "27-dev");
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(2))->m_Body).m_NewValue == "28");
}

TEST_CASE("The environment survives a rebuild of the database from the journal", "[environment][recovery]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("27", "prod")).has_value());
        REQUIRE(
            store.repo().add_entry(log_id(LOG), sample_change(1, 10, HeaderField::UNIT_NUMBER, "27-dev")).has_value());
        // With a change entry in the log, a correction fixes that entry (the value in force), not the start.
        REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::UNIT_NUMBER, "27-qa").has_value());
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
    CHECK(open->m_Log.initial_header().m_UnitNumber == UnitNumber::parse("27", "prod").value());
    CHECK(std::get<HeaderChangeEntry>(open->m_Log.find(entry_id(1))->m_Body).m_NewValue == "27-qa");
    CHECK(store.repo().open_log().value()->m_Log.entries().size() == 1);
}

TEST_CASE("The journal of an older build, with the digits alone, rebuilds the database", "[environment][recovery]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_change(1, 10, HeaderField::UNIT_NUMBER, "28")).has_value());
    }
    // This build wrote the digits alone for a unit without an environment, as the build before it did.
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    REQUIRE(journal.find("\"unit\":\"27\"") != std::string::npos);
    REQUIRE(journal.find("-prod") == std::string::npos);
    std::filesystem::remove(dir.path() / "ej.db");
    REQUIRE(recover(StorageLocation(dir.path())).has_value());

    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_UnitNumber == UnitNumber::parse("27").value());
    CHECK(open->m_Log.initial_header().m_UnitNumber->environment().empty());
}

TEST_CASE("A stored unit with a damaged environment is reported as corruption, not read as something else",
          "[environment][sqlite][damage]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with("27", "prod")).has_value());
    }
    for (const char* damaged : {"27-Prod", "27-", "27--", "27-pro d", "-prod", "x-prod"})
    {
        INFO(damaged);
        TempDir copy_dir;
        std::filesystem::copy_file(dir.path() / "ej.db", copy_dir.path() / "ej.db");
        {
            RawDb raw(copy_dir.path() / "ej.db");
            REQUIRE(raw.exec(QStringLiteral("UPDATE day_log SET unit_number = ?"), {QString::fromLatin1(damaged)}));
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
