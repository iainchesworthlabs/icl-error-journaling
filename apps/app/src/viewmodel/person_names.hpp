// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/ids.hpp"

#include <optional>
#include <string>

namespace ej::viewmodel
{

/// Where the export gets a person's name from (Stage 14, 3.6). Production: the people store; a test: a fixed list. An
/// entry holds a person's id and never the name, so every line that names someone asks here at the moment it is
/// rendered. The answer is none for a person who was forgotten or is not known.
class PersonNames
{
public:
    PersonNames() = default;
    PersonNames(const PersonNames&) = delete;
    PersonNames& operator=(const PersonNames&) = delete;
    virtual ~PersonNames() = default;

    [[nodiscard]] virtual std::optional<std::string> name_of(const PersonId& id) const = 0;
};

} // namespace ej::viewmodel
