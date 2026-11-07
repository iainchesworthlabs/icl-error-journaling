// SPDX-License-Identifier: GPL-3.0-or-later
#include <catch2/catch_test_macros.hpp>

#include "icl_template/parse.hpp"

using icl_template::ParseError;
using icl_template::SemanticVersion;
using icl_template::parse_version;

TEST_CASE("parse_version reads MAJOR.MINOR.PATCH", "[parse]")
{
    const auto version = parse_version("1.22.333");
    REQUIRE(version.has_value());
    CHECK(*version == SemanticVersion{1, 22, 333});
}

TEST_CASE("parse_version reports why it failed", "[parse]")
{
    CHECK(parse_version("").error() == ParseError::EMPTY);
    CHECK(parse_version("1.2").error() == ParseError::WRONG_FIELD_COUNT);
    CHECK(parse_version("1.2.3.4").error() == ParseError::WRONG_FIELD_COUNT);
    CHECK(parse_version("1.x.3").error() == ParseError::NOT_A_NUMBER);
    CHECK(parse_version("1..3").error() == ParseError::NOT_A_NUMBER);
    CHECK(parse_version("1.2.3 ").error() == ParseError::NOT_A_NUMBER);
    CHECK(parse_version("1.2.1234567890").error() == ParseError::OUT_OF_RANGE);
    CHECK(parse_version("-1.2.3").error() == ParseError::NOT_A_NUMBER);
}

TEST_CASE("SemanticVersion compares field by field", "[parse]")
{
    CHECK(SemanticVersion{1, 2, 3} == SemanticVersion{1, 2, 3});
    CHECK(SemanticVersion{1, 2, 3} != SemanticVersion{0, 2, 3});
    CHECK(SemanticVersion{1, 2, 3} != SemanticVersion{1, 0, 3});
    CHECK(SemanticVersion{1, 2, 3} != SemanticVersion{1, 2, 0});
    CHECK(*parse_version("0.0.0") == SemanticVersion{});
}
