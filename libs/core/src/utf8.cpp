// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/utf8.hpp"

#include <cstdint>

namespace ej
{

std::optional<std::size_t> utf8_code_points(std::string_view text)
{
    std::size_t count = 0;
    std::size_t i = 0;
    const std::size_t size = text.size();
    const auto byte_at = [&text](std::size_t index) { return static_cast<std::uint8_t>(text[index]); };
    const auto is_continuation = [](std::uint8_t b) { return (b & 0xC0U) == 0x80U; };

    while (i < size)
    {
        const std::uint8_t lead = byte_at(i);
        if (lead < 0x80U)
        {
            i += 1;
        }
        else if (lead >= 0xC2U && lead <= 0xDFU)
        {
            if (i + 1 >= size || !is_continuation(byte_at(i + 1)))
            {
                return std::nullopt;
            }
            i += 2;
        }
        else if (lead >= 0xE0U && lead <= 0xEFU)
        {
            if (i + 2 >= size || !is_continuation(byte_at(i + 1)) || !is_continuation(byte_at(i + 2)))
            {
                return std::nullopt;
            }
            const std::uint8_t second = byte_at(i + 1);
            if ((lead == 0xE0U && second < 0xA0U) || (lead == 0xEDU && second > 0x9FU)) // overlong, surrogate
            {
                return std::nullopt;
            }
            i += 3;
        }
        else if (lead >= 0xF0U && lead <= 0xF4U)
        {
            if (i + 3 >= size || !is_continuation(byte_at(i + 1)) || !is_continuation(byte_at(i + 2)) ||
                !is_continuation(byte_at(i + 3)))
            {
                return std::nullopt;
            }
            const std::uint8_t second = byte_at(i + 1);
            if ((lead == 0xF0U && second < 0x90U) || (lead == 0xF4U && second > 0x8FU)) // overlong, above U+10FFFF
            {
                return std::nullopt;
            }
            i += 4;
        }
        else
        {
            return std::nullopt;
        }
        ++count;
    }
    return count;
}

} // namespace ej
