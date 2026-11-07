// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/lifecycle.hpp"

#include <QCoreApplication>
#include <QObject>
#include <functional>
#include <optional>

namespace ej::bridge
{

/// The event for an application state: Inactive, Suspended and Hidden all raise BECAME_INACTIVE (a share sheet or a
/// system dialog makes the application inactive for a moment and costs one cheap flush), Active raises BECAME_ACTIVE.
[[nodiscard]] std::optional<LifecycleEvent> lifecycle_event_for(Qt::ApplicationState state) noexcept;

/// The only code that listens to Qt for the lifecycle: `QGuiApplication::applicationStateChanged` (when the application
/// is a QGuiApplication) and `aboutToQuit` become LifecycleEvents for the coordinator, which is plain C++. Whether Qt
/// delivers Inactive and Suspended as iOS sends them is assumption A2 of docs/plan/stage-5.md, read from the
/// `lifecycle:` log lines on a device.
class LifecycleAdapter : public QObject
{
    Q_OBJECT

public:
    LifecycleAdapter(QCoreApplication& application, std::function<void(LifecycleEvent)> handler,
                     QObject* parent = nullptr);

public slots:
    void on_state_changed(Qt::ApplicationState state);
    void on_about_to_quit();

private:
    std::function<void(LifecycleEvent)> m_Handler;
};

} // namespace ej::bridge
