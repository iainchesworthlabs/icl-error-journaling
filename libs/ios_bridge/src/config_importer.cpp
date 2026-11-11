// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/config_importer.hpp"

#include "error_journaling/android_import_plan.hpp"

#include "error_journaling/document_types.hpp"
#include "error_journaling/share_presenter.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <filesystem>
#include <utility>

namespace ej::bridge
{

ConfigImporter::ConfigImporter(Load load, QObject* parent) : QObject(parent), m_Load(std::move(load)) {}

void ConfigImporter::import_file(const QString& path)
{
    m_Waiting.append(path);
    if (!m_Busy)
    {
        next();
    }
}

void ConfigImporter::pick_and_import(DocumentPicker& picker)
{
    picker.pick_configuration(
        [this](const PickResult& result)
        {
            switch (result.m_Outcome)
            {
            case PickOutcome::PICKED:
                import_file(QString::fromStdU16String(result.m_Path.u16string()));
                break;
            case PickOutcome::CANCELLED:
                break;
            case PickOutcome::UNAVAILABLE:
            case PickOutcome::FAILED:
                emit failed({tr("The file could not be opened.")});
                break;
            }
        });
}

void ConfigImporter::load_finished()
{
    if (!m_Busy)
    {
        return;
    }
    QFile::remove(m_Current);
    m_Current.clear();
    m_Busy = false;
    next();
}

void ConfigImporter::next()
{
    while (!m_Busy && !m_Waiting.isEmpty())
    {
        const QString path = m_Waiting.takeFirst();
        const QFileInfo info(path);
        if (!info.exists() || !info.isFile())
        {
            emit failed({tr("The file could not be read.")});
            continue;
        }
        if (static_cast<std::uintmax_t>(info.size()) > CONFIG_MAX_BYTES)
        {
            // Refused before it is read, and deleted: it was a copy made for this import.
            QFile::remove(path);
            emit failed({tr("That file is too large to be a setup file.")});
            continue;
        }
        m_Current = path;
        m_Busy = true;
        m_Load(path);
    }
    if (!m_Busy)
    {
        emit idle();
    }
}

QString export_directory(const QString& temporary_directory)
{
    return QDir(temporary_directory).filePath(QStringLiteral("ej-share"));
}

QString import_directory(const QString& temporary_directory)
{
    return QDir(temporary_directory)
        .filePath(QString::fromUtf8(android_import_directory_name.data(),
                                    static_cast<qsizetype>(android_import_directory_name.size())));
}

std::size_t purge_import_leftovers(const QString& documents_directory, const QString& temporary_directory)
{
    std::size_t removed = 0;
    if (!documents_directory.isEmpty())
    {
        const QString inbox = QDir(documents_directory).filePath(QStringLiteral("Inbox"));
        removed += purge_directory(std::filesystem::path(inbox.toStdU16String()));
    }
    if (!temporary_directory.isEmpty())
    {
        removed += purge_directory(std::filesystem::path(export_directory(temporary_directory).toStdU16String()));
        removed += purge_directory(std::filesystem::path(import_directory(temporary_directory).toStdU16String()));
    }
    return removed;
}

} // namespace ej::bridge
