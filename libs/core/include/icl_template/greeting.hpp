// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace icl_template
{

/// Builds a greeting for a name. Kept deliberately small: it exists so that the template
/// has a library, a test and an application that all build and run on every platform.
class Greeter
{
public:
    explicit Greeter(std::string name);

    /// "Hello, <name>, from <host>." with the host name from the platform layer.
    [[nodiscard]] std::string greet() const;

    [[nodiscard]] std::string_view name() const noexcept
    {
        return m_Name;
    }

private:
    std::string m_Name;
};

}  // namespace icl_template
