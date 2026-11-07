// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace ej::viewmodel
{

/// The observers of one subject, for the plain C++ observer interfaces (`LogObserver`, `ConfigurationObserver`,
/// `OpenLogObserver`). GUI thread only. An observer may add or remove observers, itself included, while it is being
/// told; one removed during a notification is not told afterwards, one added is not told until the next.
template <typename Observer>
class ObserverList
{
public:
    /// An observer that is already listed is not listed twice.
    void add(Observer& observer)
    {
        if (!contains(observer))
        {
            m_Observers.push_back(&observer);
        }
    }

    void remove(Observer& observer)
    {
        std::erase(m_Observers, &observer);
    }

    [[nodiscard]] bool contains(const Observer& observer) const
    {
        return std::ranges::find(m_Observers, &observer) != m_Observers.end();
    }

    [[nodiscard]] std::size_t size() const
    {
        return m_Observers.size();
    }
    [[nodiscard]] bool empty() const
    {
        return m_Observers.empty();
    }

    /// Calls `call(observer)` for each observer, in the order they were added.
    template <typename Call>
    void notify(Call&& call) const
    {
        const std::vector<Observer*> snapshot = m_Observers;
        for (Observer* observer : snapshot)
        {
            if (std::ranges::find(m_Observers, observer) != m_Observers.end())
            {
                call(*observer);
            }
        }
    }

private:
    std::vector<Observer*> m_Observers;
};

} // namespace ej::viewmodel
