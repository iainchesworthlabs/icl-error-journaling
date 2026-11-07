// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <string_view>

namespace ej::bridge
{

/// Catches the file the system opens in the application (AirDrop, Files "Open in", Share): Qt's iOS plugin turns
/// `application:openURL:options:` into a `QFileOpenEvent` (assumption A1 of docs/plan/stage-5.md, checked on a device).
/// Installed on the application before the engine loads.
///
/// It keeps the paths until `ready()` is called, so a file that launched the application cold is not lost to the
/// start-up order, and hands them out one at a time: the next only after `handled()`. A URL that is not a local file,
/// an empty one, and a directory are ignored.
class OpenFileFilter : public QObject
{
    Q_OBJECT

public:
    /// `log` receives one line per file event (no path, only that it arrived), for the first device pass.
    explicit OpenFileFilter(std::function<void(std::string_view)> log = {}, QObject* parent = nullptr);

    /// The importer can take files now.
    void ready();
    /// The file handed out last has been dealt with; hand out the next.
    void handled();

    [[nodiscard]] int pending() const noexcept
    {
        return static_cast<int>(m_Pending.size());
    }

signals:
    /// A file to import, a local path. Not emitted again until `handled()`.
    void fileOpened(const QString& path);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void deliver();

    std::function<void(std::string_view)> m_Log;
    QStringList m_Pending;
    bool m_Ready = false;
    bool m_Busy = false;
};

} // namespace ej::bridge
