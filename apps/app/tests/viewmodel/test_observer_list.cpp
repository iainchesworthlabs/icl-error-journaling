// SPDX-License-Identifier: GPL-3.0-or-later
#include "viewmodel/observer_list.hpp"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <string>
#include <vector>

using ej::viewmodel::ObserverList;

namespace
{

struct Listener
{
    std::string m_Name;
    std::vector<std::string>* m_Calls;
    std::function<void()> m_OnCall;

    void hear()
    {
        m_Calls->push_back(m_Name);
        if (m_OnCall)
        {
            m_OnCall();
        }
    }
};

} // namespace

TEST_CASE("An observer list tells its observers in the order they were added", "[viewmodel][observer]")
{
    std::vector<std::string> calls;
    Listener first{"first", &calls, {}};
    Listener second{"second", &calls, {}};
    ObserverList<Listener> list;
    list.add(first);
    list.add(second);

    list.notify([](Listener& listener) { listener.hear(); });

    CHECK(calls == std::vector<std::string>{"first", "second"});
    CHECK(list.size() == 2);
    CHECK_FALSE(list.empty());
}

TEST_CASE("An observer added twice is told once, and a removed one is not told", "[viewmodel][observer]")
{
    std::vector<std::string> calls;
    Listener first{"first", &calls, {}};
    Listener second{"second", &calls, {}};
    ObserverList<Listener> list;
    list.add(first);
    list.add(first);
    list.add(second);
    CHECK(list.size() == 2);
    CHECK(list.contains(second));

    list.remove(first);
    list.remove(first); // removing what is not there changes nothing
    list.notify([](Listener& listener) { listener.hear(); });

    CHECK(calls == std::vector<std::string>{"second"});
    CHECK_FALSE(list.contains(first));
}

TEST_CASE("An observer that removes a later observer while being told keeps it from being told",
          "[viewmodel][observer]")
{
    std::vector<std::string> calls;
    ObserverList<Listener> list;
    Listener second{"second", &calls, {}};
    Listener first{"first", &calls, [&] { list.remove(second); }};
    list.add(first);
    list.add(second);

    list.notify([](Listener& listener) { listener.hear(); });

    CHECK(calls == std::vector<std::string>{"first"});
}

TEST_CASE("An observer may remove itself, and one added during a notification waits for the next",
          "[viewmodel][observer]")
{
    std::vector<std::string> calls;
    ObserverList<Listener> list;
    Listener late{"late", &calls, {}};
    Listener first{"first", &calls, {}};
    first.m_OnCall = [&]
    {
        list.remove(first);
        list.add(late);
    };
    list.add(first);

    list.notify([](Listener& listener) { listener.hear(); });
    CHECK(calls == std::vector<std::string>{"first"});

    calls.clear();
    list.notify([](Listener& listener) { listener.hear(); });
    CHECK(calls == std::vector<std::string>{"late"});
}

TEST_CASE("An empty observer list tells nobody", "[viewmodel][observer]")
{
    ObserverList<Listener> list;
    int told = 0;
    list.notify([&](Listener&) { ++told; });
    CHECK(told == 0);
    CHECK(list.empty());
}
