// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/uuid.hpp"

#include <compare>
#include <functional>

namespace ej
{

/// A `Uuid` that cannot be passed where another kind of id is expected.
template <typename Tag>
class TypedId
{
public:
    constexpr TypedId() = default;
    constexpr explicit TypedId(const Uuid& value) : m_Value(value) {}

    [[nodiscard]] constexpr const Uuid& uuid() const { return m_Value; }
    [[nodiscard]] std::string format() const { return m_Value.format(); }

    friend constexpr auto operator<=>(const TypedId&, const TypedId&) = default;

private:
    Uuid m_Value;
};

struct EntryIdTag;
struct ErrorTypeIdTag;
struct NoteTemplateIdTag;
struct LocationIdTag;
struct LogIdTag;

using EntryId = TypedId<EntryIdTag>;
using ErrorTypeId = TypedId<ErrorTypeIdTag>;
using NoteTemplateId = TypedId<NoteTemplateIdTag>;
using LocationId = TypedId<LocationIdTag>;
/// The id of one log (a day's log). The caller generates it as a UUID v4 before the call that creates the log, which is
/// what makes replaying a journal idempotent.
using LogId = TypedId<LogIdTag>;

}  // namespace ej

template <typename Tag>
struct std::hash<ej::TypedId<Tag>>
{
    std::size_t operator()(const ej::TypedId<Tag>& id) const noexcept { return std::hash<ej::Uuid>{}(id.uuid()); }
};
