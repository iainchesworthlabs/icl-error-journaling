// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// What the in-app help remembers (Stage 10, package 10.6; docs/plan/stage-10.md, "The first-run tip"): whether the
// first-run tip of the Log screen has been closed, and on which days it was shown.
//
// The tip is on screen until it is closed or it has been shown on three days, whichever comes first. It is never shown
// again after Close. This is a setting of the application and not a part of a log, so it lives in its own small file
// beside the configuration file and nothing in it reaches the store, the journals or a setup file. A file that cannot
// be read or written is treated as empty and the tip is shown as on a first run; the person is never told, because
// losing the tip costs nothing.

#include <QDate>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>

namespace ej::viewmodel
{

class HelpState final : public QObject
{
    Q_OBJECT
    /// Whether the Log screen may show the tip: it has not been closed and it has not been shown on three days.
    Q_PROPERTY(bool tipOpen READ tip_open NOTIFY tipOpenChanged)

public:
    /// How many days the tip is shown for when it is not closed.
    static constexpr int TIP_DAYS = 3;

    /// `file` is the path of the settings file (created on the first write). `today` gives the date of the device; it
    /// is a parameter so that a test chooses the day.
    HelpState(QString file, std::function<QDate()> today, QObject* parent = nullptr);

    bool tip_open() const;

    /// The tip is on screen: counts today as a day it was shown. The Log screen calls it when the card appears.
    Q_INVOKABLE void noteTipShown();

    /// The Close button: the tip is never shown again.
    Q_INVOKABLE void closeTip();

signals:
    void tipOpenChanged();

private:
    void load();
    void save() const;
    bool open_now() const;

    QString m_File;
    std::function<QDate()> m_Today;
    bool m_Closed = false;
    QStringList m_Days; // ISO dates on which the tip was shown, at most TIP_DAYS
    bool m_Open = true;
};

} // namespace ej::viewmodel
