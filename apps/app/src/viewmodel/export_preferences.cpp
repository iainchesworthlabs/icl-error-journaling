// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/export_preferences.hpp"

#include "error_journaling/export_format.hpp"

#include <QSettings>
#include <utility>

namespace ej::viewmodel
{

namespace
{
const QString KEY_NAME_EXPORT = QStringLiteral("export/nameExport");
const QString KEY_MARKER = QStringLiteral("export/interactionMarker");

QString from_std(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

std::string clean_marker(const QString& typed)
{
    QString text = typed;
    for (qsizetype i = 0; i < text.size(); ++i)
    {
        if (text.at(i).unicode() < 0x20 || text.at(i).unicode() == 0x7F)
        {
            text.truncate(i);
            break;
        }
    }
    text = text.trimmed();
    if (text.size() > ExportPreferences::MAX_MARKER_CHARACTERS)
    {
        text.truncate(ExportPreferences::MAX_MARKER_CHARACTERS);
        if (text.back().isHighSurrogate())
        {
            text.chop(1);
        }
        text = text.trimmed();
    }
    return text.toStdString();
}
} // namespace

ExportPreferences::ExportPreferences(QString file, QObject* parent)
    : QObject(parent), m_File(std::move(file)), m_Marker(export_format::DEFAULT_INTERACTION_MARKER)
{
    load();
}

QString ExportPreferences::name_export() const
{
    return from_std(name_level_key(m_Level));
}

QString ExportPreferences::interaction_marker() const
{
    return QString::fromStdString(m_Marker);
}

void ExportPreferences::set_name_export(const QString& level)
{
    const auto wanted = name_level_from_key(level.toStdString());
    if (!wanted || *wanted == m_Level)
    {
        return;
    }
    m_Level = *wanted;
    save();
    emit nameExportChanged();
}

void ExportPreferences::set_interaction_marker(const QString& marker)
{
    std::string wanted = clean_marker(marker);
    if (wanted == m_Marker)
    {
        return;
    }
    m_Marker = std::move(wanted);
    save();
    emit interactionMarkerChanged();
}

void ExportPreferences::load()
{
    if (m_File.isEmpty())
    {
        return;
    }
    const QSettings settings(m_File, QSettings::IniFormat);
    // A damaged or unknown value is not a decision of the person: it reads as the default, which is the careful one.
    m_Level =
        name_level_from_key(settings.value(KEY_NAME_EXPORT).toString().toStdString()).value_or(NameLevel::INITIALS);
    if (settings.contains(KEY_MARKER))
    {
        m_Marker = clean_marker(settings.value(KEY_MARKER).toString());
    }
}

void ExportPreferences::save() const
{
    if (m_File.isEmpty())
    {
        return;
    }
    QSettings settings(m_File, QSettings::IniFormat);
    settings.setValue(KEY_NAME_EXPORT, name_export());
    settings.setValue(KEY_MARKER, interaction_marker());
    settings.sync();
}

} // namespace ej::viewmodel
