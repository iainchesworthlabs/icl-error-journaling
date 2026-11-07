// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/lifecycle.hpp"

#include <utility>

namespace ej::bridge
{

LifecycleCoordinator::LifecycleCoordinator(LifecycleActions actions, BackgroundTasks& tasks)
    : m_Actions(std::move(actions)), m_Tasks(tasks)
{
}

void LifecycleCoordinator::on_event(LifecycleEvent event)
{
    switch (event)
    {
    case LifecycleEvent::BECAME_INACTIVE:
        log("lifecycle: inactive");
        if (m_Actions.set_application_active)
        {
            m_Actions.set_application_active(false);
        }
        flush_in_task();
        break;
    case LifecycleEvent::BECAME_ACTIVE:
        log("lifecycle: active");
        if (m_Actions.set_application_active)
        {
            m_Actions.set_application_active(true);
        }
        if (m_StorageReady)
        {
            call(m_Actions.check_day);
        }
        else
        {
            m_CheckWaiting = true;
        }
        break;
    case LifecycleEvent::ABOUT_TO_QUIT:
        log("lifecycle: quit");
        flush_now();
        break;
    }
}

void LifecycleCoordinator::storage_ready()
{
    if (m_StorageReady)
    {
        return;
    }
    m_StorageReady = true;
    if (m_CheckWaiting)
    {
        m_CheckWaiting = false;
        call(m_Actions.check_day);
    }
}

void LifecycleCoordinator::flush_in_task()
{
    // The expiry handler ends the task if the system's time runs out first; the normal path ends it after the flush.
    // Either way it ends once.
    const BackgroundTasks::Token token = m_Tasks.begin("flush on inactive",
                                                       [this]
                                                       {
                                                           if (m_OpenTask)
                                                           {
                                                               const BackgroundTasks::Token open = *m_OpenTask;
                                                               m_OpenTask.reset();
                                                               log("lifecycle: flush task expired");
                                                               m_Tasks.end(open);
                                                           }
                                                       });
    m_OpenTask = token;
    flush_now();
    if (m_OpenTask)
    {
        const BackgroundTasks::Token open = *m_OpenTask;
        m_OpenTask.reset();
        m_Tasks.end(open);
    }
}

void LifecycleCoordinator::flush_now()
{
    call(m_Actions.flush_draft);
    call(m_Actions.drain);
}

void LifecycleCoordinator::log(std::string_view line) const
{
    if (m_Actions.log)
    {
        m_Actions.log(line);
    }
}

void LifecycleCoordinator::call(const std::function<void()>& action) const
{
    if (action)
    {
        action();
    }
}

} // namespace ej::bridge
