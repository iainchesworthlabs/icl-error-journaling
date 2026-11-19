// SPDX-License-Identifier: GPL-3.0-or-later
// The small preference files across the rename of Interactions to Observations (T-O4-1, T-O4-2): a file written by the
// build before it keeps the switch the person turned on, the board last used and the marker word, and the new build
// writes only the new names. All data is synthetic.
#include "error_journaling/export_format.hpp"
#include "viewmodel/board_state.hpp"
#include "viewmodel/export_preferences.hpp"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

using namespace ej;
using viewmodel::BoardState;
using viewmodel::ExportPreferences;

namespace
{

/// The file as the build before the rename wrote it (`legacy`).
void write_old_file(const QString& path, const QString& marker = {}, bool with_board = true)
{
    QSettings settings(path, QSettings::IniFormat);
    if (with_board)
    {
        settings.setValue(QStringLiteral("boards/interactionsEnabled"), true);
        settings.setValue(QStringLiteral("boards/last"), QStringLiteral("interactions"));
    }
    settings.setValue(QStringLiteral("export/nameExport"), QStringLiteral("initials"));
    if (!marker.isEmpty())
    {
        settings.setValue(QStringLiteral("export/interactionMarker"), marker);
    }
    settings.sync();
}

} // namespace

TEST_CASE("prefs.read_old_files: the switch and the last board written before the rename still open the second board",
          "[unit][legacy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board-state.ini"));
    write_old_file(path);

    {
        BoardState state(path);
        CHECK(state.interactions_enabled());
        CHECK(state.last_board() == BoardState::INTERACTIONS);
        // Saving writes the new names only, and the old value of the board is read for ever.
        state.set_last_board(BoardState::ERRORS);
        state.set_last_board(BoardState::INTERACTIONS);
    }
    const QSettings written(path, QSettings::IniFormat);
    CHECK(written.value(QStringLiteral("boards/observationsEnabled")).toString() == QStringLiteral("true"));
    CHECK(written.value(QStringLiteral("boards/last")).toString() == QStringLiteral("observations"));

    const BoardState reopened(path);
    CHECK(reopened.interactions_enabled());
    CHECK(reopened.last_board() == BoardState::INTERACTIONS);
}

TEST_CASE("prefs.read_old_files: the board is off in an old file that never turned it on", "[unit][legacy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("board-state.ini"));
    write_old_file(path, {}, false);
    const BoardState state(path);
    CHECK_FALSE(state.interactions_enabled());
    CHECK(state.last_board() == BoardState::ERRORS);
}

TEST_CASE("prefs.default_marker_policy: an old default marker reads as the new default, a typed word is kept",
          "[unit][legacy]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());

    SECTION("the old default was never a choice")
    {
        const QString path = dir.filePath(QStringLiteral("a.ini"));
        write_old_file(path, QStringLiteral("(Interaction)"));
        const ExportPreferences preferences(path);
        CHECK(preferences.interaction_marker() == QStringLiteral("(Observation)"));
    }
    SECTION("a word the person typed is kept as typed")
    {
        const QString path = dir.filePath(QStringLiteral("b.ini"));
        write_old_file(path, QStringLiteral("(Chat)"));
        ExportPreferences preferences(path);
        CHECK(preferences.interaction_marker() == QStringLiteral("(Chat)"));
        // The new build writes the new key, and reads it back.
        preferences.set_interaction_marker(QStringLiteral("(Talk)"));
        const QSettings written(path, QSettings::IniFormat);
        CHECK(written.value(QStringLiteral("export/observationMarker")).toString() == QStringLiteral("(Talk)"));
        CHECK(ExportPreferences(path).interaction_marker() == QStringLiteral("(Talk)"));
    }
    SECTION("an empty marker, which drops the label, is a choice and is kept")
    {
        const QString path = dir.filePath(QStringLiteral("c.ini"));
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue(QStringLiteral("export/interactionMarker"), QString());
            settings.sync();
        }
        CHECK(ExportPreferences(path).interaction_marker().isEmpty());
    }
    SECTION("without a stored marker the default is the new word")
    {
        CHECK(ExportPreferences(dir.filePath(QStringLiteral("d.ini"))).interaction_marker() ==
              QString::fromUtf8(export_format::DEFAULT_INTERACTION_MARKER.data()));
        CHECK(ExportPreferences(QString()).interaction_marker() == QStringLiteral("(Observation)"));
    }
    SECTION("the new key wins over the old one")
    {
        const QString path = dir.filePath(QStringLiteral("e.ini"));
        write_old_file(path, QStringLiteral("(Chat)"));
        {
            QSettings settings(path, QSettings::IniFormat);
            settings.setValue(QStringLiteral("export/observationMarker"), QStringLiteral("(Talk)"));
            settings.sync();
        }
        CHECK(ExportPreferences(path).interaction_marker() == QStringLiteral("(Talk)"));
    }
}
