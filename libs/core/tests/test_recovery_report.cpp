// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/recovery_report.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace ej;

TEST_CASE("A clean recovery report is one short sentence", "[recovery-report]")
{
    RecoveryReport report;
    report.m_OpenLogEntries = 1;
    CHECK(describe(report) == "clean: 1 entry in the open log");
}

TEST_CASE("A recovery report names every count that is not zero", "[recovery-report]")
{
    RecoveryReport report;
    report.m_Outcome = RecoveryOutcome::REBUILT;
    report.m_OpenLogEntries = 312;
    report.m_RebuiltRecords = 330;
    report.m_DiscardedRecords = 2;
    report.m_DiscardedTailBytes = 17;
    report.m_VoidedRecords = 1;
    report.m_ArchivedLogsLost = 1;
    report.m_DraftRecovered = true;
    report.m_QuarantinedPaths = {"quarantine/a/ej.db", "quarantine/a/ej.db-wal"};
    CHECK(describe(report) ==
          "rebuilt: 312 entries in the open log, 330 records rebuilt, 2 records discarded, 17 bytes of torn journal "
          "tail removed, 1 write rolled back, 1 archived log lost, note draft recovered, 2 files quarantined");
}

TEST_CASE("The sentence leaves out SQLite's own message", "[recovery-report]")
{
    RecoveryReport report;
    report.m_IntegrityMessage = "page 7 is never used";
    CHECK(describe(report).find("page 7") == std::string::npos);
}

TEST_CASE("Every recovery outcome has a name", "[recovery-report]")
{
    for (int i = 0; i <= static_cast<int>(RecoveryOutcome::UNRECOVERABLE); ++i)
    {
        CHECK(recovery_outcome_name(static_cast<RecoveryOutcome>(i)) != "unknown");
    }
}

TEST_CASE("A replayed recovery says how many records it applied", "[recovery-report]")
{
    RecoveryReport report;
    report.m_Outcome = RecoveryOutcome::REPLAYED;
    report.m_OpenLogEntries = 12;
    report.m_ReplayedRecords = 1;
    CHECK(describe(report) == "replayed: 12 entries in the open log, 1 record replayed");
}

TEST_CASE("A value outside the enumeration is named unknown, not crashed on", "[recovery-report]")
{
    CHECK(recovery_outcome_name(static_cast<RecoveryOutcome>(99)) == "unknown");
}
