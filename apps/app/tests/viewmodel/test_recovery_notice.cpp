// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/recovery_notice.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <set>

using namespace ej;
using viewmodel::recovery_notice;

namespace
{

/// Text that must never reach the screen: it stands for entry text, a path and the database's own message.
RecoveryReport report_with_private_text(RecoveryOutcome outcome)
{
    RecoveryReport report;
    report.m_Outcome = outcome;
    report.m_OpenLogEntries = 6;
    report.m_ReplayedRecords = 3;
    report.m_RebuiltRecords = 40;
    report.m_DiscardedRecords = 2;
    report.m_ArchivedLogsLost = 1;
    report.m_QuarantinedPaths = {"/data/Sam Example/quarantine-1.db"};
    report.m_IntegrityMessage = "Wrong Response page 9 is damaged";
    report.m_Notes = {"North Site journal seeded"};
    return report;
}

} // namespace

TEST_CASE("A clean start has no recovery notice", "[viewmodel][recovery_notice]")
{
    CHECK(recovery_notice(RecoveryReport{}).isEmpty());
    CHECK(recovery_notice(report_with_private_text(RecoveryOutcome::CLEAN)).isEmpty());
}

TEST_CASE("Each outcome that recovered something has its own plain sentence", "[viewmodel][recovery_notice]")
{
    std::set<QString> sentences;
    for (const auto outcome : {RecoveryOutcome::REPLAYED, RecoveryOutcome::REBUILT, RecoveryOutcome::PARTIAL_LOSS,
                               RecoveryOutcome::UNRECOVERABLE})
    {
        const QString notice = recovery_notice(report_with_private_text(outcome));
        INFO(recovery_outcome_name(outcome));
        CHECK_FALSE(notice.isEmpty());
        CHECK_FALSE(notice.contains(QChar('\n')));
        CHECK(notice.endsWith(QChar('.')));
        sentences.insert(notice);
    }
    CHECK(sentences.size() == 4);
}

TEST_CASE("The notice states counts and nothing from the report's text", "[viewmodel][recovery_notice]")
{
    for (const auto outcome : {RecoveryOutcome::REPLAYED, RecoveryOutcome::REBUILT, RecoveryOutcome::PARTIAL_LOSS,
                               RecoveryOutcome::UNRECOVERABLE})
    {
        const QString notice = recovery_notice(report_with_private_text(outcome));
        INFO(recovery_outcome_name(outcome));
        CHECK_FALSE(notice.contains(QStringLiteral("Sam Example")));
        CHECK_FALSE(notice.contains(QStringLiteral("Wrong Response")));
        CHECK_FALSE(notice.contains(QStringLiteral("North Site")));
        CHECK_FALSE(notice.contains(QStringLiteral("quarantine")));
        CHECK_FALSE(notice.contains(QStringLiteral("damaged")));
        CHECK_FALSE(notice.contains(QStringLiteral("%1")));
    }

    CHECK(recovery_notice(report_with_private_text(RecoveryOutcome::REPLAYED)).contains(QStringLiteral("3 changes")));
    CHECK(recovery_notice(report_with_private_text(RecoveryOutcome::REBUILT)).contains(QStringLiteral("40 saved")));
    CHECK(recovery_notice(report_with_private_text(RecoveryOutcome::PARTIAL_LOSS)).contains(QStringLiteral("2 saved")));
}

TEST_CASE("A count of one reads as a singular", "[viewmodel][recovery_notice]")
{
    RecoveryReport report;
    report.m_Outcome = RecoveryOutcome::REPLAYED;
    report.m_ReplayedRecords = 1;
    CHECK(recovery_notice(report).contains(QStringLiteral("1 change was")));
    report.m_Outcome = RecoveryOutcome::REBUILT;
    report.m_RebuiltRecords = 1;
    CHECK(recovery_notice(report).contains(QStringLiteral("1 saved record.")));
    report.m_Outcome = RecoveryOutcome::PARTIAL_LOSS;
    report.m_DiscardedRecords = 1;
    CHECK(recovery_notice(report).contains(QStringLiteral("1 saved record could")));
}
