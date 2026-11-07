// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/crash_point.hpp"

#include <array>

namespace ej
{

namespace
{

constexpr std::array<std::pair<CrashPoint, std::string_view>, 8> NAMES{{
    {CrashPoint::AFTER_JOURNAL_APPEND, "after_journal_append"},
    {CrashPoint::AFTER_JOURNAL_SYNC, "after_journal_sync"},
    {CrashPoint::BEFORE_DB_COMMIT, "before_db_commit"},
    {CrashPoint::AFTER_DB_COMMIT, "after_db_commit"},
    {CrashPoint::MID_CHECKPOINT, "mid_checkpoint"},
    {CrashPoint::MID_ARCHIVE_PAIR, "mid_archive_pair"},
    {CrashPoint::MID_RECOVERY, "mid_recovery"},
    {CrashPoint::MID_MIGRATION, "mid_migration"},
}};

}  // namespace

std::string_view crash_point_name(CrashPoint point)
{
    for (const auto& [value, name] : NAMES)
    {
        if (value == point)
        {
            return name;
        }
    }
    return {};
}

std::optional<CrashPoint> crash_point_from_name(std::string_view name)
{
    for (const auto& [value, text] : NAMES)
    {
        if (text == name)
        {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace ej
