// SPDX-License-Identifier: GPL-3.0-or-later
#include "icl_template/greeting.hpp"

#include <utility>

#include "icl_template/platform.hpp"

namespace icl_template
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

}  // namespace icl_template
