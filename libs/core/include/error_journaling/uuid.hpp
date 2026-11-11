// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "error_journaling/result.hpp"

namespace ej
{

/// A 16 byte identifier, written as lower-case 8-4-4-4-12 hexadecimal. The core reads no random source and no clock:
/// the caller supplies the random words to `from_random`.
class Uuid
{
public:
    enum class Error
    {
        WRONG_LENGTH,
        BAD_SEPARATOR,
        NOT_HEX
    };

    using Bytes = std::array<std::uint8_t, 16>;

    constexpr Uuid() = default;
    constexpr explicit Uuid(const Bytes& bytes) : m_Bytes(bytes) {}

    /// Accepts upper and lower case hexadecimal; nothing else (no braces, no missing hyphens).
    [[nodiscard]] static Result<Uuid, Error> parse(std::string_view text);

    /// A version 4 (random) identifier built from two caller-supplied random words.
    [[nodiscard]] static Uuid from_random(std::uint64_t high, std::uint64_t low);

    [[nodiscard]] std::string format() const;
    [[nodiscard]] constexpr const Bytes& bytes() const { return m_Bytes; }
    [[nodiscard]] constexpr bool is_nil() const { return m_Bytes == Bytes{}; }

    friend constexpr auto operator<=>(const Uuid&, const Uuid&) = default;

private:
    Bytes m_Bytes{};
};

}  // namespace ej

template <>
struct std::hash<ej::Uuid>
{
    std::size_t operator()(const ej::Uuid& id) const noexcept
    {
        // FNV-1a over 64 bits, whatever the width of size_t: the constants do not fit a 32-bit size_t.
        std::uint64_t h = 1469598103934665603ULL;
        for (const auto b : id.bytes())
        {
            h = (h ^ b) * 1099511628211ULL;
        }
        return static_cast<std::size_t>(h);
    }
};
