// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/utf8.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using ej::utf8_code_points;

TEST_CASE("utf8_code_points counts code points, not bytes", "[utf8]")
{
    CHECK(utf8_code_points("").value() == 0);
    CHECK(utf8_code_points("Sam Example").value() == 11);
    CHECK(utf8_code_points("caf\xC3\xA9").value() == 4);          // e with an acute accent, two bytes
    CHECK(utf8_code_points("\xE2\x86\x92").value() == 1);         // an arrow, three bytes
    CHECK(utf8_code_points("\xF0\x9F\x98\x80").value() == 1);     // an emoji, four bytes
    CHECK(utf8_code_points(std::string("a\0b", 3)).value() == 3); // NUL is a code point
    CHECK(utf8_code_points("\xF4\x8F\xBF\xBF").value() == 1);     // U+10FFFF, the last one
}

TEST_CASE("utf8_code_points rejects malformed text", "[utf8]")
{
    CHECK_FALSE(utf8_code_points("\x80"));                 // a continuation byte alone
    CHECK_FALSE(utf8_code_points("\xC3"));                 // cut short
    CHECK_FALSE(utf8_code_points("\xC0\x80"));             // overlong NUL
    CHECK_FALSE(utf8_code_points("\xE0\x80\x80"));         // overlong
    CHECK_FALSE(utf8_code_points("\xED\xA0\x80"));         // a surrogate
    CHECK_FALSE(utf8_code_points("\xF4\x90\x80\x80"));     // above U+10FFFF
    CHECK_FALSE(utf8_code_points("\xF8\x88\x80\x80\x80")); // five bytes
    CHECK_FALSE(utf8_code_points("\xFF"));
    CHECK_FALSE(utf8_code_points("ok\xE2\x86")); // cut short at the end
}
