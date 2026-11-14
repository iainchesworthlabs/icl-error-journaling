// SPDX-License-Identifier: GPL-3.0-or-later
//
// The versions besides the robot's (HDR-10) in the real store: the database column and the journal carry the stored text,
// they survive a restart and a correction, and a database from the build before the column existed migrates with every
// log in it intact.
#include "error_journaling/storage_location.hpp"
#include "recovery_fixture.hpp"

using namespace ej;
using namespace ej::test;

namespace
{

constexpr std::uint64_t LOG = 1;

HeaderValues header_with_extras()
{
    HeaderValues header = sample_header();
    header.m_ExtraVersions = {LabelledVersion{"Influencer", SoftwareVersion::parse("4.5.0").value()}};
    return header;
}

}  // namespace

TEST_CASE("Extra versions are stored in the database and the journal and read back", "[extra-versions][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), header_with_extras()).has_value());
        REQUIRE(store.repo()
                    .correct_header(log_id(LOG), HeaderField::EXTRA_VERSIONS, "Influencer=4.6.0\nGateway=2.0.1")
                    .has_value());
    }
    {
        RawDb raw(dir.path() / "ej.db");
        CHECK(raw.scalar(QStringLiteral("SELECT extra_versions FROM day_log")).toString().toStdString() ==
              "Influencer=4.6.0\nGateway=2.0.1");
    }
    const std::string journal = read_bytes(journal_of(dir.path(), log_id(LOG)));
    CHECK(journal.find("\"extra_versions\":\"Influencer=4.5.0\"") != std::string::npos);

    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_ExtraVersions.size() == 2);
    CHECK(open->m_Log.initial_header().m_ExtraVersions.at(0).m_Version.format() == "4.6.0");
    const auto logs = store.repo().list_logs().value();
    REQUIRE(logs.size() == 1);
    CHECK(logs[0].m_Header.m_ExtraVersions.size() == 2);
    CHECK(logs[0].m_Header.m_SoftwareVersion == SoftwareVersion::parse("1.4.2").value());
}

TEST_CASE("A log without extra versions leaves the column empty and reads with none", "[extra-versions][sqlite]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
    }
    {
        RawDb raw(dir.path() / "ej.db");
        CHECK(raw.scalar(QStringLiteral("SELECT extra_versions IS NULL FROM day_log")).toInt() == 1);
    }
    OpenStore store(dir.path());
    CHECK(store.repo().open_log().value()->m_Log.initial_header().m_ExtraVersions.empty());
}

TEST_CASE("A database from before the column existed migrates and keeps its log and its version",
          "[extra-versions][sqlite][migration]")
{
    TempDir dir;
    {
        OpenStore store(dir.path());
        REQUIRE(store.repo().create_log(log_id(LOG), sample_header()).has_value());
        REQUIRE(store.repo().add_entry(log_id(LOG), sample_error(1, 5)).has_value());
    }
    {
        // The database as the schema 2 build left it: no extra_versions column.
        RawDb raw(dir.path() / "ej.db");
        REQUIRE(raw.drop_interaction_schema());
        REQUIRE(raw.exec(QStringLiteral("ALTER TABLE day_log DROP COLUMN extra_versions")));
        REQUIRE(raw.exec(QStringLiteral("PRAGMA user_version=2")));
    }
    // The launch check migrates it (a backup is made first), as the app does before it opens the store.
    const auto report = recover(StorageLocation(dir.path()));
    REQUIRE(report.has_value());
    OpenStore store(dir.path());
    const auto open = store.repo().open_log().value();
    REQUIRE(open.has_value());
    CHECK(open->m_Log.initial_header().m_SoftwareVersion == SoftwareVersion::parse("1.4.2").value());
    CHECK(open->m_Log.initial_header().m_ExtraVersions.empty());
    CHECK(open->m_Log.entries().size() == 1);
    REQUIRE(store.repo().correct_header(log_id(LOG), HeaderField::EXTRA_VERSIONS, "Influencer=4.5.0").has_value());
}
