// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/lifecycle_adapter.hpp"

#include <QGuiApplication>
#include <utility>

namespace ej::bridge
{

std::optional<LifecycleEvent> lifecycle_event_for(Qt::ApplicationState state) noexcept
{
    switch (state)
    {
    case Qt::ApplicationActive:
        return LifecycleEvent::BECAME_ACTIVE;
    case Qt::ApplicationInactive:
    case Qt::ApplicationSuspended:
    case Qt::ApplicationHidden:
        return LifecycleEvent::BECAME_INACTIVE;
    }
    return std::nullopt;
}

LifecycleAdapter::LifecycleAdapter(QCoreApplication& application, std::function<void(LifecycleEvent)> handler,
                                   QObject* parent)
    : QObject(parent), m_Handler(std::move(handler))
{
    connect(&application, &QCoreApplication::aboutToQuit, this, &LifecycleAdapter::on_about_to_quit);
    if (auto* gui = qobject_cast<QGuiApplication*>(&application))
    {
        connect(gui, &QGuiApplication::applicationStateChanged, this, &LifecycleAdapter::on_state_changed);
    }
}

void LifecycleAdapter::on_state_changed(Qt::ApplicationState state)
{
    if (const auto event = lifecycle_event_for(state); event && m_Handler)
    {
        m_Handler(*event);
    }
}

void LifecycleAdapter::on_about_to_quit()
{
    if (m_Handler)
    {
        m_Handler(LifecycleEvent::ABOUT_TO_QUIT);
    }
}

} // namespace ej::bridge
