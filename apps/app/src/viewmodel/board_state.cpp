// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/board_state.hpp"

#include <QSettings>
#include <utility>

namespace ej::viewmodel
{

namespace
{
const QString KEY_ENABLED = QStringLiteral("boards/observationsEnabled");
const QString KEY_LAST = QStringLiteral("boards/last");
// legacy: the names written before the rename of Interactions to Observations. Read for ever, never written.
const QString LEGACY_KEY_ENABLED = QStringLiteral("boards/interactionsEnabled");
// The word kept in the file for the second board. In memory (and in QML) the board keeps its internal id INTERACTIONS.
const QString STORED_OBSERVATIONS = QStringLiteral("observations");
const QString LEGACY_STORED_INTERACTIONS = QStringLiteral("interactions");

bool stored_is_second_board(const QString& stored)
{
    return stored.compare(STORED_OBSERVATIONS, Qt::CaseInsensitive) == 0 ||
           stored.compare(LEGACY_STORED_INTERACTIONS, Qt::CaseInsensitive) == 0;
}
} // namespace

BoardState::BoardState(QString file, QObject* parent) : QObject(parent), m_File(std::move(file))
{
    load();
}

bool BoardState::interactions_enabled() const
{
    return m_Enabled;
}

QString BoardState::last_board() const
{
    return m_Last;
}

void BoardState::set_interactions_enabled(bool enabled)
{
    if (enabled == m_Enabled)
    {
        return;
    }
    m_Enabled = enabled;
    const bool left_board = !enabled && m_Last == INTERACTIONS;
    if (left_board)
    {
        m_Last = ERRORS;
    }
    save();
    emit interactionsEnabledChanged();
    if (left_board)
    {
        emit lastBoardChanged();
    }
}

void BoardState::set_last_board(const QString& board)
{
    const QString wanted = (board == INTERACTIONS && m_Enabled) ? INTERACTIONS : ERRORS;
    if (wanted == m_Last)
    {
        return;
    }
    m_Last = wanted;
    save();
    emit lastBoardChanged();
}

void BoardState::load()
{
    if (m_File.isEmpty())
    {
        return;
    }
    const QSettings settings(m_File, QSettings::IniFormat);
    // Only the word "true" turns the board on: a damaged value is not a decision of the person.
    const QString enabled = settings.contains(KEY_ENABLED) ? settings.value(KEY_ENABLED).toString()
                                                           : settings.value(LEGACY_KEY_ENABLED).toString();
    m_Enabled = enabled == QStringLiteral("true");
    m_Last = (m_Enabled && stored_is_second_board(settings.value(KEY_LAST).toString())) ? INTERACTIONS : ERRORS;
}

void BoardState::save() const
{
    if (m_File.isEmpty())
    {
        return;
    }
    QSettings settings(m_File, QSettings::IniFormat);
    settings.setValue(KEY_ENABLED, m_Enabled);
    settings.setValue(KEY_LAST, m_Last == INTERACTIONS ? STORED_OBSERVATIONS : m_Last);
    settings.sync();
}

} // namespace ej::viewmodel
