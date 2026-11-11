// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/ids.hpp"
#include "error_journaling/uuid.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <functional>
#include <type_traits>
#include <unordered_set>

using ej::Uuid;

TEST_CASE("Uuid parses and formats lower-case 8-4-4-4-12", "[uuid]")
{
    const auto id = Uuid::parse("0F8FAD5B-D9CB-469F-A165-70867728950E");
    REQUIRE(id.has_value());
    CHECK(id->format() == "0f8fad5b-d9cb-469f-a165-70867728950e");
    CHECK(id->bytes()[0] == 0x0F);
    CHECK(id->bytes()[15] == 0x0E);
    CHECK_FALSE(id->is_nil());
    CHECK(Uuid{}.is_nil());
    CHECK(Uuid{}.format() == "00000000-0000-0000-0000-000000000000");
}

TEST_CASE("Uuid rejects malformed text", "[uuid]")
{
    CHECK(Uuid::parse("").error() == Uuid::Error::WRONG_LENGTH);
    CHECK(Uuid::parse("0f8fad5bd9cb469fa16570867728950e").error() == Uuid::Error::WRONG_LENGTH);
    CHECK(Uuid::parse("0f8fad5b_d9cb-469f-a165-70867728950e").error() == Uuid::Error::BAD_SEPARATOR);
    CHECK(Uuid::parse("0f8fad5b-d9cb-469f-a165-7086772895xe").error() == Uuid::Error::NOT_HEX);
}

TEST_CASE("Uuid from_random sets version 4 and the RFC 4122 variant", "[uuid]")
{
    const auto id = Uuid::from_random(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
    CHECK(id.format() == "ffffffff-ffff-4fff-bfff-ffffffffffff");
    const auto zero = Uuid::from_random(0, 0);
    CHECK(zero.format() == "00000000-0000-4000-8000-000000000000");
    CHECK(Uuid::parse(id.format()).value() == id);
}

TEST_CASE("Typed ids are distinct, ordered and hashable", "[uuid]")
{
    const auto a = Uuid::from_random(1, 2);
    const auto b = Uuid::from_random(3, 4);
    const ej::EntryId entry_a(a);
    const ej::EntryId entry_b(b);
    CHECK(entry_a == ej::EntryId(a));
    CHECK(entry_a != entry_b);
    CHECK(entry_a < entry_b);
    CHECK(entry_a.format() == a.format());
    CHECK_FALSE(std::is_convertible_v<ej::EntryId, ej::LocationId>);

    std::unordered_set<ej::ErrorTypeId> set;
    set.insert(ej::ErrorTypeId(a));
    set.insert(ej::ErrorTypeId(a));
    set.insert(ej::ErrorTypeId(b));
    CHECK(set.size() == 2);
}

TEST_CASE("The Uuid hash is FNV-1a over 64 bits, narrowed to size_t", "[uuid][unit]")
{
    // The constants are 64-bit: on a 32-bit size_t (armeabi-v7a) they used to be truncated before the loop, which the
    // compiler there rejects. The hash is computed in 64 bits and only the result is narrowed.
    const auto id = Uuid::parse("0f8fad5b-d9cb-469f-a165-70867728950e");
    REQUIRE(id.has_value());
    std::uint64_t expected = 1469598103934665603ULL;
    for (const auto b : id->bytes())
    {
        expected = (expected ^ b) * 1099511628211ULL;
    }
    CHECK(std::hash<Uuid>{}(*id) == static_cast<std::size_t>(expected));
    CHECK(std::hash<Uuid>{}(*id) == std::hash<Uuid>{}(*id));
}
