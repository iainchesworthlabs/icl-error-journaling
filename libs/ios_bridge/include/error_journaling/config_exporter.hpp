// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <functional>

namespace ej::bridge
{

/// The other direction of PRF-08: writes the setup to a temporary `.ejconfig` file and hands it to the share sheet,
/// which deletes it when the sheet ends (QmlBridge::shareTemporaryFile). The settings model writes the file
/// (`SettingsModel::exportTo`) and reports `exportFinished`, which the caller connects to `export_finished()`.
///
/// One export at a time. A file left from an earlier run is removed before the new one is written. When writing fails
/// the file is removed and `failed` carries the message.
class ConfigExporter : public QObject
{
    Q_OBJECT

public:
    using ExportTo = std::function<void(const QString& path)>;
    using ShareFile = std::function<void(const QString& path)>;

    /// `directory` is where the file is made (`export_directory(temp)`).
    ConfigExporter(QString directory, ExportTo export_to, ShareFile share_file, QObject* parent = nullptr);

    /// Settings > Export setup.
    void request();

    /// The settings model has written the file, or failed to.
    void export_finished(bool ok, const QString& message);

    [[nodiscard]] bool busy() const noexcept
    {
        return m_Busy;
    }

signals:
    void failed(const QString& message);

private:
    QString m_Directory;
    ExportTo m_ExportTo;
    ShareFile m_ShareFile;
    QString m_Path;
    bool m_Busy = false;
};

} // namespace ej::bridge
