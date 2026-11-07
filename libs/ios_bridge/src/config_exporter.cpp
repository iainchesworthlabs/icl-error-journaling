// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/config_exporter.hpp"

#include "error_journaling/document_types.hpp"

#include <QDir>
#include <QFile>
#include <utility>

namespace ej::bridge
{

ConfigExporter::ConfigExporter(QString directory, ExportTo export_to, ShareFile share_file, QObject* parent)
    : QObject(parent), m_Directory(std::move(directory)), m_ExportTo(std::move(export_to)),
      m_ShareFile(std::move(share_file))
{
}

void ConfigExporter::request()
{
    if (m_Busy)
    {
        return;
    }
    if (!QDir().mkpath(m_Directory))
    {
        emit failed(tr("The setup could not be prepared for sharing."));
        return;
    }
    const QString name =
        QString::fromUtf8(CONFIG_EXPORT_FILE_NAME.data(), static_cast<qsizetype>(CONFIG_EXPORT_FILE_NAME.size()));
    m_Path = QDir(m_Directory).filePath(name);
    QFile::remove(m_Path); // a stale copy
    m_Busy = true;
    m_ExportTo(m_Path);
}

void ConfigExporter::export_finished(bool ok, const QString& message)
{
    if (!m_Busy)
    {
        return;
    }
    m_Busy = false;
    if (!ok)
    {
        QFile::remove(m_Path);
        emit failed(message);
        return;
    }
    m_ShareFile(m_Path);
}

} // namespace ej::bridge
