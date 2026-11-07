// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/greeting.hpp"

#include <utility>

#include "error_journaling/platform.hpp"

namespace error_journaling
{

Greeter::Greeter(std::string name) : m_Name(std::move(name))
{
}

std::string Greeter::greet() const
{
    std::string text = "Hello, ";
    text += m_Name;
    text += ", from ";
    text += platform::host_name();
    text += ".";
    return text;
}

}  // namespace error_journaling
