// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/uuid.hpp"

namespace ej
{

namespace
{

int hex_value(char c)
{
    if (c >= '0' && c <= '9')
    {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f')
    {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F')
    {
        return c - 'A' + 10;
    }
    return -1;
}

bool is_separator_position(std::size_t i)
{
    return i == 8 || i == 13 || i == 18 || i == 23;
}

}  // namespace

Result<Uuid, Uuid::Error> Uuid::parse(std::string_view text)
{
    if (text.size() != 36)
    {
        return fail(Error::WRONG_LENGTH);
    }
    Bytes bytes{};
    std::size_t out = 0;
    int pending = -1;
    for (std::size_t i = 0; i < text.size(); ++i)
    {
        if (is_separator_position(i))
        {
            if (text[i] != '-')
            {
                return fail(Error::BAD_SEPARATOR);
            }
            continue;
        }
        const int v = hex_value(text[i]);
        if (v < 0)
        {
            return fail(Error::NOT_HEX);
        }
        if (pending < 0)
        {
            pending = v;
        }
        else
        {
            bytes[out++] = static_cast<std::uint8_t>((pending << 4) | v);
            pending = -1;
        }
    }
    return Uuid(bytes);
}

Uuid Uuid::from_random(std::uint64_t high, std::uint64_t low)
{
    Bytes bytes{};
    for (int i = 0; i < 8; ++i)
    {
        bytes[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(high >> (56 - 8 * i));
        bytes[static_cast<std::size_t>(8 + i)] = static_cast<std::uint8_t>(low >> (56 - 8 * i));
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);  // version 4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);  // RFC 4122 variant
    return Uuid(bytes);
}

std::string Uuid::format() const
{
    static constexpr std::string_view DIGITS = "0123456789abcdef";
    std::string out;
    out.reserve(36);
    for (std::size_t i = 0; i < m_Bytes.size(); ++i)
    {
        if (i == 4 || i == 6 || i == 8 || i == 10)
        {
            out.push_back('-');
        }
        out.push_back(DIGITS[m_Bytes[i] >> 4]);
        out.push_back(DIGITS[m_Bytes[i] & 0x0F]);
    }
    return out;
}

}  // namespace ej
