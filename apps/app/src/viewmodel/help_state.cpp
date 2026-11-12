// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/help_state.hpp"

#include <QSettings>
#include <utility>

namespace ej::viewmodel
{

namespace
{
const QString KEY_CLOSED = QStringLiteral("firstRunTip/closed");
const QString KEY_DAYS = QStringLiteral("firstRunTip/days");
} // namespace

HelpState::HelpState(QString file, std::function<QDate()> today, QObject* parent)
    : QObject(parent), m_File(std::move(file)), m_Today(std::move(today))
{
    load();
    m_Open = open_now();
}

bool HelpState::tip_open() const
{
    return m_Open;
}

bool HelpState::open_now() const
{
    if (m_Closed)
    {
        return false;
    }
    if (m_Days.size() < TIP_DAYS)
    {
        return true;
    }
    return m_Days.contains(m_Today().toString(Qt::ISODate));
}

void HelpState::noteTipShown()
{
    const QString today = m_Today().toString(Qt::ISODate);
    if (m_Closed || m_Days.contains(today) || m_Days.size() >= TIP_DAYS)
    {
        return;
    }
    m_Days.append(today);
    save();
}

void HelpState::closeTip()
{
    if (m_Closed)
    {
        return;
    }
    m_Closed = true;
    save();
    if (m_Open)
    {
        m_Open = false;
        emit tipOpenChanged();
    }
}

void HelpState::load()
{
    if (m_File.isEmpty())
    {
        return;
    }
    const QSettings settings(m_File, QSettings::IniFormat);
    // Only the word "true" closes the tip: a damaged value is not a decision of the person.
    m_Closed = settings.value(KEY_CLOSED).toString() == QStringLiteral("true");
    m_Days = settings.value(KEY_DAYS).toStringList();
    // A date that is not one is dropped, so a damaged file cannot keep the tip from showing or make it count days.
    QStringList valid;
    for (const QString& day : std::as_const(m_Days))
    {
        if (QDate::fromString(day, Qt::ISODate).isValid() && !valid.contains(day))
        {
            valid.append(day);
        }
    }
    m_Days = valid.mid(0, TIP_DAYS);
}

void HelpState::save() const
{
    if (m_File.isEmpty())
    {
        return;
    }
    QSettings settings(m_File, QSettings::IniFormat);
    settings.setValue(KEY_CLOSED, m_Closed);
    settings.setValue(KEY_DAYS, m_Days);
    settings.sync();
}

} // namespace ej::viewmodel
