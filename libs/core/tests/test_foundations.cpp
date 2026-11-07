// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/header_field.hpp"
#include "error_journaling/result.hpp"
#include "golden.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace
{

enum class DemoError
{
    BAD
};

ej::Result<int, DemoError> halve(int v)
{
    if (v % 2 != 0)
    {
        return ej::fail(DemoError::BAD);
    }
    return v / 2;
}

}  // namespace

TEST_CASE("fail builds an unexpected of the error enum", "[result]")
{
    CHECK(halve(4).value() == 2);
    const auto odd = halve(3);
    REQUIRE_FALSE(odd.has_value());
    CHECK(odd.error() == DemoError::BAD);
}

TEST_CASE("HeaderField keys round-trip", "[header-field]")
{
    CHECK(ej::header_field_key(ej::HeaderField::UNIT_NUMBER) == "unit_number");
    CHECK(ej::header_field_key(ej::HeaderField::LOCATION) == "location");
    CHECK(ej::header_field_key(ej::HeaderField::SOFTWARE_VERSION) == "software_version");
    for (const auto f : {ej::HeaderField::UNIT_NUMBER, ej::HeaderField::LOCATION, ej::HeaderField::SOFTWARE_VERSION})
    {
        CHECK(ej::header_field_from_key(ej::header_field_key(f)) == f);
    }
    CHECK_FALSE(ej::header_field_from_key("facilitator").has_value());
}

TEST_CASE("first_difference names the first differing line", "[golden]")
{
    using ej::test::first_difference;
    CHECK_FALSE(first_difference("a\nb", "a\nb").has_value());
    CHECK(first_difference("a\nb\nc", "a\nx\nc").value() == "line 2: expected \"b\", actual \"x\"");
    CHECK(first_difference("a\n", "a").value() == "line 2: expected \"\", actual end of text");
    CHECK(first_difference("a", "a\nb").value() == "line 2: expected end of text, actual \"b\"");
}

TEST_CASE("compare_golden reads bytes exactly and reports a mismatch", "[golden]")
{
    const auto bytes = ej::test::read_bytes(ej::test::fixture_path("helper-selftest.txt"));
    REQUIRE(bytes.has_value());
    CHECK(*bytes == "fixture contents\n");

    const std::string text = "First line\n\xe2\x80\x93 second \xe2\x86\x92 line\nlast line, no trailing newline";
    CHECK_GOLDEN("helper-selftest", text);

    const auto mismatch = ej::test::compare_golden("helper-selftest", "First line\nother");
    REQUIRE(mismatch.has_value());
    CHECK(mismatch->find("line 2") != std::string::npos);
    CHECK(ej::test::read_bytes(std::filesystem::path(EJ_GOLDEN_ACTUAL_DIR) / "helper-selftest.actual.txt")
              .value_or("") == "First line\nother");

    CHECK(ej::test::compare_golden("no-such-golden", "").has_value());
}
