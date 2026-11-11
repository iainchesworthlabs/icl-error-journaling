// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/document_picker.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <cstddef>
#include <functional>

namespace ej::bridge
{

/// Takes a setup file from the picker or the open-file event to the settings, and deletes it. The file holds
/// work-specific values: a copy left in Documents/Inbox would be backed up and would outlive the import, so it is
/// deleted on every outcome (imported, refused, too large, unreadable).
///
/// It does not read or parse the file. It checks the size first (the Stage 1 limit of 1 MiB) so an oversize file is
/// never handed on, then calls `load` with the path; the settings model (`SettingsModel::loadImport`) reads it, checks
/// it and reports `importPrepared` or `importFailed`, and the caller connects either to `load_finished()`, which
/// deletes the file and moves to the next. Its own refusals come as `failed`, with plain-English messages. One file at
/// a time; the rest wait. Main thread only.
class ConfigImporter : public QObject
{
    Q_OBJECT

public:
    using Load = std::function<void(const QString& path)>;

    explicit ConfigImporter(Load load, QObject* parent = nullptr);

    /// A file from the open-file event or the picker. A `.ejconfig` and a `.json` are treated alike.
    void import_file(const QString& path);

    /// Shows the picker and imports what it returns. Cancelled is silent; anything else that is not a file gives one
    /// `failed` message.
    void pick_and_import(DocumentPicker& picker);

    /// The file handed to `load` has been dealt with, whatever the result: delete it and go on.
    void load_finished();

    [[nodiscard]] bool busy() const noexcept
    {
        return m_Busy;
    }

signals:
    /// The importer itself refused the file or the picker failed. The settings model reports its own refusals.
    void failed(const QStringList& messages);
    /// Nothing is being imported and nothing is waiting.
    void idle();

private:
    void next();

    Load m_Load;
    QStringList m_Waiting;
    QString m_Current;
    bool m_Busy = false;
};

/// Removes the files a past import may have left: `Documents/Inbox` (where the system copies a file opened in the
/// application), the directory the export is written to, and the directory the Android picker and "Open with" copy to. Run at launch. Returns how many files went; a directory
/// that does not exist is not an error and nothing else is touched.
std::size_t purge_import_leftovers(const QString& documents_directory, const QString& temporary_directory);

/// The directory under the temporary directory that the export is written to.
[[nodiscard]] QString export_directory(const QString& temporary_directory);

/// The directory under the temporary directory that Android copies a picked or opened setup file to
/// (`android_import_directory_name`). It is purged at launch on every platform, and empty where nothing writes to it.
[[nodiscard]] QString import_directory(const QString& temporary_directory);

} // namespace ej::bridge
