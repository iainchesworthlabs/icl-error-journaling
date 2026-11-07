// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "viewmodel/move_only_function.hpp"

#include <QObject>
#include <utility>

namespace ej::viewmodel
{

/// The object `ThreadedStorageGateway` queues its "completions are ready" call to. It lives on the GUI thread. The call
/// is made by name and carries no argument, so the only code that reads the queued event is Qt's own: the completions
/// themselves wait in the gateway's mutex-guarded queue, where ThreadSanitizer can see how they are handed over.
class DeliveryReceiver final : public QObject
{
    Q_OBJECT

public:
    explicit DeliveryReceiver(MoveOnlyFunction<void()> deliver) : m_Deliver(std::move(deliver)) {}

    Q_INVOKABLE void deliver()
    {
        m_Deliver();
    }

private:
    MoveOnlyFunction<void()> m_Deliver;
};

} // namespace ej::viewmodel
