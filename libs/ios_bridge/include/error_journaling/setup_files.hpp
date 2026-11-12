// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>

namespace ej::bridge
{

class ConfigExporter;
class ConfigImporter;
class DocumentPicker;

/// What the QML shell sees of the setup file (PRF-08, PRF-09), registered as the singleton `SetupFiles` of the module.
/// A file reaches the shell from two places, the document picker and a file the system opens in the application
/// (AirDrop, Files), and arrives the same way: `fileReady(path, displayName)`, which the shell answers by starting the
/// import flow (its confirmation sheet, then `settings.loadImport(path)`). The importer deletes the file when the
/// settings model has answered. Export is the other direction: `exportSetup()` writes a temporary file and opens the
/// share sheet.
///
/// The importer, the picker and the exporter are not owned and must outlive this object. GUI thread only.
class SetupFiles : public QObject
{
    Q_OBJECT

public:
    SetupFiles(ConfigImporter& importer, DocumentPicker& picker, ConfigExporter& exporter, QObject* parent = nullptr);

    /// The importer has a file the shell should take to the import flow. Called by the importer's load function.
    void offer(const QString& path);

    /// Shows the document picker. The result comes as `fileReady`, or `failed` when the file cannot be used. Cancelling
    /// is silent.
    Q_INVOKABLE void pick();

    /// Writes the setup to a temporary file and opens the share sheet with it; the sheet deletes it.
    Q_INVOKABLE void exportSetup();

signals:
    /// A setup file is ready to import: a local path and the name to show ("From field-test-setup.ejconfig").
    void fileReady(const QString& path, const QString& displayName);
    /// Something the shell should tell the person: a file that was too large, or could not be read or opened, or an
    /// export that could not be prepared.
    void failed(const QString& message);

private:
    ConfigImporter& m_Importer;
    DocumentPicker& m_Picker;
    ConfigExporter& m_Exporter;
};

} // namespace ej::bridge
