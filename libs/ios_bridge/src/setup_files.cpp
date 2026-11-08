// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/setup_files.hpp"

#include "error_journaling/config_exporter.hpp"
#include "error_journaling/config_importer.hpp"
#include "error_journaling/document_picker.hpp"

#include <QFileInfo>

namespace ej::bridge
{

SetupFiles::SetupFiles(ConfigImporter& importer, DocumentPicker& picker, ConfigExporter& exporter, QObject* parent)
    : QObject(parent), m_Importer(importer), m_Picker(picker), m_Exporter(exporter)
{
    // The importer's refusals (too large, unreadable, the picker failed) and the exporter's reach the shell as one
    // message.
    connect(&m_Importer, &ConfigImporter::failed, this,
            [this](const QStringList& messages)
            {
                if (!messages.isEmpty())
                {
                    emit failed(messages.front());
                }
            });
    connect(&m_Exporter, &ConfigExporter::failed, this, &SetupFiles::failed);
}

void SetupFiles::offer(const QString& path)
{
    emit fileReady(path, QFileInfo(path).fileName());
}

void SetupFiles::pick()
{
    m_Importer.pick_and_import(m_Picker);
}

void SetupFiles::exportSetup()
{
    m_Exporter.request();
}

} // namespace ej::bridge
