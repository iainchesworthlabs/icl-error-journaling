// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/note_template.hpp"
#include "error_journaling/software_version.hpp"
#include "error_journaling/unit_number.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <string_view>

using ej::NoteTemplate;
using ej::PatternError;
using ej::SoftwareVersion;
using ej::UnitNumber;

TEST_CASE("UnitNumber accepts plain digits", "[unit-number]")
{
    for (const char* text : {"1", "27", "100", "123456789"})
    {
        INFO(text);
        const auto parsed = UnitNumber::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->digits() == text);
    }
}

TEST_CASE("UnitNumber rejects leading zeros, non-digits, empty and long input", "[unit-number]")
{
    CHECK(UnitNumber::parse("027").error() == UnitNumber::Error::LEADING_ZERO);
    CHECK(UnitNumber::parse("00").error() == UnitNumber::Error::LEADING_ZERO);
    CHECK(UnitNumber::parse("0").error() == UnitNumber::Error::LEADING_ZERO);
    CHECK(UnitNumber::parse("").error() == UnitNumber::Error::EMPTY);
    for (const char* text : {"2 7", " 27", "27 ", "-27", "+27", "2.7", "UNIT-27", "\xEF\xBC\x92\xEF\xBC\x97"})
    {
        INFO(text);
        CHECK(UnitNumber::parse(text).error() == UnitNumber::Error::NOT_DIGITS);
    }
    CHECK(UnitNumber::parse("1234567890").error() == UnitNumber::Error::TOO_LONG);
    CHECK(UnitNumber::parse("99999999999999").error() == UnitNumber::Error::TOO_LONG);
}

TEST_CASE("UnitNumber formats with the prefix verbatim", "[unit-number]")
{
    const auto n = UnitNumber::parse("27").value();
    CHECK(n.format("UNIT") == "UNIT-27");
    CHECK(n.format("Demo Unit") == "Demo Unit-27");
    CHECK(ej::unit_label("UNIT") == "UNIT #");
    CHECK(n == UnitNumber::parse("27").value());
    CHECK_FALSE(n == UnitNumber::parse("28").value());
}

TEST_CASE("A unit number may carry an environment; it is optional", "[unit-number][environment]")
{
    const auto plain = UnitNumber::parse("27").value();
    CHECK(plain.environment().empty());
    CHECK(plain.stored() == "27");
    CHECK(plain.format("UNIT") == "UNIT-27");

    const auto prod = UnitNumber::parse("27", "prod").value();
    CHECK(prod.digits() == "27");
    CHECK(prod.environment() == "prod");
    CHECK(prod.stored() == "27-prod");
    CHECK(prod.format("UNIT") == "UNIT-27-prod");
    CHECK(ej::unit_label("UNIT") == "UNIT #");

    // An empty environment is no environment, and the same unit.
    CHECK(UnitNumber::parse("27", "").value() == plain);
    CHECK_FALSE(prod == plain);
    CHECK_FALSE(prod == UnitNumber::parse("27", "dev").value());
    CHECK(prod == UnitNumber::parse("27", "prod").value());
}

TEST_CASE("An environment is lower-case letters, digits and dashes", "[unit-number][environment]")
{
    for (const char* name : {"prod", "dev", "uat2", "pre-prod", "2", "a-b-c", "x"})
    {
        INFO(name);
        CHECK(UnitNumber::is_valid_environment(name));
        CHECK(UnitNumber::parse("27", name)->environment() == name);
    }
    for (const char* name : {"Prod", "PROD", "pre prod", "pre_prod", "-prod", "prod-", "-", "pr.od", "prod\n",
                             "pr\xC3\xB6"
                             "d"})
    {
        INFO(name);
        CHECK_FALSE(UnitNumber::is_valid_environment(name));
        CHECK(UnitNumber::parse("27", name).error() == UnitNumber::Error::BAD_ENVIRONMENT);
    }
    CHECK_FALSE(UnitNumber::is_valid_environment(""));
    CHECK(UnitNumber::is_valid_environment(std::string(UnitNumber::MAX_ENVIRONMENT_LENGTH, 'a')));
    CHECK_FALSE(UnitNumber::is_valid_environment(std::string(UnitNumber::MAX_ENVIRONMENT_LENGTH + 1, 'a')));
}

TEST_CASE("The digits are judged before the environment, and a typed number never holds a dash",
          "[unit-number][environment]")
{
    CHECK(UnitNumber::parse("027", "prod").error() == UnitNumber::Error::LEADING_ZERO);
    CHECK(UnitNumber::parse("", "prod").error() == UnitNumber::Error::EMPTY);
    CHECK(UnitNumber::parse("2a", "Prod").error() == UnitNumber::Error::NOT_DIGITS);
    // `parse(digits)` is the digits alone: the stored form is not accepted where a person types digits.
    CHECK(UnitNumber::parse("27-prod").error() == UnitNumber::Error::NOT_DIGITS);
}

TEST_CASE("The stored text of a unit reads back, and text from before environments is all digits",
          "[unit-number][environment]")
{
    for (const char* text : {"27", "1", "123456789", "27-prod", "27-dev", "9-pre-prod", "5-2"})
    {
        INFO(text);
        const auto parsed = UnitNumber::parse_stored(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->stored() == text);
    }
    // Old data: digits only, no environment.
    CHECK(UnitNumber::parse_stored("27").value() == UnitNumber::parse("27").value());
    CHECK(UnitNumber::parse_stored("27-prod").value() == UnitNumber::parse("27", "prod").value());
    // A dash ends the digits at the first one; the rest is the environment, dashes and all.
    CHECK(UnitNumber::parse_stored("9-pre-prod")->environment() == "pre-prod");

    CHECK(UnitNumber::parse_stored("").error() == UnitNumber::Error::EMPTY);
    CHECK(UnitNumber::parse_stored("-prod").error() == UnitNumber::Error::EMPTY);
    CHECK(UnitNumber::parse_stored("x-prod").error() == UnitNumber::Error::NOT_DIGITS);
    CHECK(UnitNumber::parse_stored("027-prod").error() == UnitNumber::Error::LEADING_ZERO);
    CHECK(UnitNumber::parse_stored("27-").error() == UnitNumber::Error::BAD_ENVIRONMENT);
    CHECK(UnitNumber::parse_stored("27--").error() == UnitNumber::Error::BAD_ENVIRONMENT);
    CHECK(UnitNumber::parse_stored("27-Prod").error() == UnitNumber::Error::BAD_ENVIRONMENT);
    CHECK(UnitNumber::parse_stored("27-pro d").error() == UnitNumber::Error::BAD_ENVIRONMENT);
    CHECK(UnitNumber::parse_stored("27-prod-").error() == UnitNumber::Error::BAD_ENVIRONMENT);
}

TEST_CASE("SoftwareVersion accepts major.minor.patch", "[software-version]")
{
    for (const char* text : {"1.4.2", "0.0.1", "10.20.30", "1.5.0", "123456789.0.0"})
    {
        INFO(text);
        const auto parsed = SoftwareVersion::parse(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->format() == text);
    }
}

TEST_CASE("SoftwareVersion rejects malformed input", "[software-version]")
{
    using Error = SoftwareVersion::Error;
    CHECK(SoftwareVersion::parse("").error() == Error::EMPTY);
    CHECK(SoftwareVersion::parse("1.4").error() == Error::WRONG_COMPONENT_COUNT);
    CHECK(SoftwareVersion::parse("1.4.2.1").error() == Error::WRONG_COMPONENT_COUNT);
    CHECK(SoftwareVersion::parse("1").error() == Error::WRONG_COMPONENT_COUNT);
    for (const char* text : {"1.4.2-beta", "1.4.2+3", "vv1.4.2", "1..2", "1.4.", ".4.2", " 1.4.2"})
    {
        INFO(text);
        CHECK(SoftwareVersion::parse(text).error() == Error::NOT_DIGITS);
    }
    CHECK(SoftwareVersion::parse("01.4.2").error() == Error::LEADING_ZERO);
    CHECK(SoftwareVersion::parse("1.04.2").error() == Error::LEADING_ZERO);
    CHECK(SoftwareVersion::parse("1.4.1234567890").error() == Error::TOO_LONG);
}

TEST_CASE("SoftwareVersion orders numerically per component", "[software-version]")
{
    const auto v = [](const char* text) { return SoftwareVersion::parse(text).value(); };
    CHECK(v("1.10.0") > v("1.9.9"));
    CHECK(v("1.4.2") < v("1.5.0"));
    CHECK(v("2.0.0") > v("1.99.99"));
    CHECK(v("1.4.2") == v("1.4.2"));
    CHECK(v("1.4.2") <= v("1.4.2"));
}

TEST_CASE("A commit is 7 to 12 hexadecimal characters, lower-cased, and a full hash is shortened to 7",
          "[software-version][commit]")
{
    const auto commit = [](std::string_view text) { return SoftwareVersion::parse("1.4.2", text); };

    // The ends of the range, and what is between.
    CHECK(commit("a1b2c3d")->commit() == "a1b2c3d");
    CHECK(commit("a1b2c3d4e5f6")->commit() == "a1b2c3d4e5f6");
    CHECK(commit("0123456789")->commit() == "0123456789");
    // Upper case is lower-cased, mixed case too.
    CHECK(commit("A1B2C3D")->commit() == "a1b2c3d");
    CHECK(commit("A1b2C3d4E5F6")->commit() == "a1b2c3d4e5f6");
    // A full 40-character hash keeps its first 7 characters, whatever the case.
    CHECK(commit("a1b2c3d4e5f6a7b8c9d0a1b2c3d4e5f6a7b8c9d0")->commit() == "a1b2c3d");
    CHECK(commit("A1B2C3D4E5F6A7B8C9D0A1B2C3D4E5F6A7B8C9D0")->commit() == "a1b2c3d");
    // Empty is no commit.
    CHECK(commit("")->commit().empty());
    CHECK(SoftwareVersion::parse("1.4.2")->commit().empty());

    using Error = SoftwareVersion::Error;
    CHECK(commit("a1b2c3").error() == Error::COMMIT_LENGTH);                                    // 6
    CHECK(commit("a1b2c3d4e5f6a").error() == Error::COMMIT_LENGTH);                             // 13
    CHECK(commit("a1b2c3d4e5f6a7b8c9d0a1b2c3d4e5f6a7b8c9").error() == Error::COMMIT_LENGTH);    // 38
    CHECK(commit("a1b2c3d4e5f6a7b8c9d0a1b2c3d4e5f6a7b8c9d0a").error() == Error::COMMIT_LENGTH); // 41
    CHECK(commit("g1b2c3d").error() == Error::COMMIT_NOT_HEX);
    CHECK(commit("a1b2c3 ").error() == Error::COMMIT_NOT_HEX);
    CHECK(commit("a1b2-c3d").error() == Error::COMMIT_NOT_HEX);
    CHECK(commit("zzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzzz").error() == Error::COMMIT_NOT_HEX); // 40, not hex
    // The version is judged before the commit.
    CHECK(SoftwareVersion::parse("1.4", "a1b2c3d").error() == Error::WRONG_COMPONENT_COUNT);
    CHECK(SoftwareVersion::parse("1.4.2", "xyz").error() == Error::COMMIT_NOT_HEX);
}

TEST_CASE("A version prints with its commit in brackets, and stores it after a plus sign", "[software-version][commit]")
{
    const auto plain = SoftwareVersion::parse("1.4.2").value();
    CHECK(plain.format() == "1.4.2");
    CHECK(plain.stored() == "1.4.2");
    CHECK(plain.display() == "v1.4.2");

    const auto with_commit = SoftwareVersion::parse("1.4.2", "A1B2C3D").value();
    CHECK(with_commit.format() == "1.4.2");
    CHECK(with_commit.stored() == "1.4.2+a1b2c3d");
    CHECK(with_commit.display() == "v1.4.2 (a1b2c3d)");
    CHECK(with_commit != plain);
    CHECK(SoftwareVersion::parse("1.4.2", "a1b2c3d").value() == with_commit);

    // The typed version never holds a plus sign: the commit has its own field.
    CHECK(SoftwareVersion::parse("1.4.2+a1b2c3d").error() == SoftwareVersion::Error::NOT_DIGITS);
}

TEST_CASE("The stored text of a version reads back, and text from before commits has no plus sign",
          "[software-version][commit]")
{
    for (const char* text : {"1.4.2", "0.0.1", "1.4.2+a1b2c3d", "1.4.2+a1b2c3d4e5f6", "10.20.30+0123456"})
    {
        INFO(text);
        const auto parsed = SoftwareVersion::parse_stored(text);
        REQUIRE(parsed.has_value());
        CHECK(parsed->stored() == text);
    }
    // Old data: three components, no commit.
    CHECK(SoftwareVersion::parse_stored("1.4.2").value() == SoftwareVersion::parse("1.4.2").value());
    CHECK(SoftwareVersion::parse_stored("1.4.2+a1b2c3d").value() == SoftwareVersion::parse("1.4.2", "a1b2c3d").value());
    // The text is normalised like a typed commit: a full hash is shortened and upper case is lower-cased.
    CHECK(SoftwareVersion::parse_stored("1.4.2+A1B2C3D").value().stored() == "1.4.2+a1b2c3d");
    CHECK(SoftwareVersion::parse_stored("1.4.2+a1b2c3d4e5f6a7b8c9d0a1b2c3d4e5f6a7b8c9d0").value().stored() ==
          "1.4.2+a1b2c3d");

    using Error = SoftwareVersion::Error;
    CHECK(SoftwareVersion::parse_stored("").error() == Error::EMPTY);
    CHECK(SoftwareVersion::parse_stored("+a1b2c3d").error() == Error::EMPTY);
    CHECK(SoftwareVersion::parse_stored("1.4+a1b2c3d").error() == Error::WRONG_COMPONENT_COUNT);
    CHECK(SoftwareVersion::parse_stored("01.4.2+a1b2c3d").error() == Error::LEADING_ZERO);
    CHECK(SoftwareVersion::parse_stored("1.4.2+").error() == Error::COMMIT_LENGTH);
    CHECK(SoftwareVersion::parse_stored("1.4.2+abc").error() == Error::COMMIT_LENGTH);
    CHECK(SoftwareVersion::parse_stored("1.4.2+xyzxyzx").error() == Error::COMMIT_NOT_HEX);
    // Only the first plus sign ends the version; a second one is not hexadecimal.
    CHECK(SoftwareVersion::parse_stored("1.4.2+a1b2c3d+e4f5a6b").error() == Error::COMMIT_NOT_HEX);
}

namespace
{

NoteTemplate make_template(std::string pattern)
{
    NoteTemplate t;
    t.m_Name = "Played song";
    t.m_ValuePrompt = "Which song?";
    t.m_OutputPattern = std::move(pattern);
    return t;
}

}  // namespace

TEST_CASE("render_note substitutes the value", "[note-template]")
{
    CHECK(ej::render_note(make_template("Played {value}"), "Morning Medley") == "Played Morning Medley");
    CHECK(ej::render_note(make_template("Visitor: {value}"), "Sam Example") == "Visitor: Sam Example");
    CHECK(ej::render_note(make_template("{value} played twice"), "Evening Song") == "Evening Song played twice");
}

TEST_CASE("render_note handles repeated placeholders and escaped braces", "[note-template]")
{
    CHECK(ej::render_note(make_template("{value} and {value}"), "x") == "x and x");
    CHECK(ej::render_note(make_template("{{x}} {value}"), "y") == "{x} y");
    CHECK(ej::validate_pattern("{{x}} {value}").has_value());
}

TEST_CASE("validate_pattern rejects bad patterns", "[note-template]")
{
    CHECK(ej::validate_pattern("Played {value}").has_value());
    CHECK(ej::validate_pattern("no placeholder").error() == PatternError::MISSING_PLACEHOLDER);
    CHECK(ej::validate_pattern("").error() == PatternError::MISSING_PLACEHOLDER);
    CHECK(ej::validate_pattern("{{value}}").error() == PatternError::MISSING_PLACEHOLDER);
    CHECK(ej::validate_pattern("{value").error() == PatternError::UNBALANCED_BRACE);
    CHECK(ej::validate_pattern("value}").error() == PatternError::UNBALANCED_BRACE);
    CHECK(ej::validate_pattern("{value}}").error() == PatternError::UNBALANCED_BRACE);
    CHECK(ej::validate_pattern("{name}").error() == PatternError::UNKNOWN_PLACEHOLDER);
    CHECK(ej::validate_pattern("{value} {name}").error() == PatternError::UNKNOWN_PLACEHOLDER);
}

TEST_CASE("render_note inserts the value verbatim, trimmed", "[note-template]")
{
    CHECK(ej::render_note(make_template("Played {value}"), "{value}") == "Played {value}");
    CHECK(ej::render_note(make_template("Played {value}"), "a { b }") == "Played a { b }");
    CHECK(ej::render_note(make_template("Played {value}"), "  Morning Medley \t\n") == "Played Morning Medley");
    CHECK(ej::render_note(make_template("Played {value}"), "   ") == "Played ");
    CHECK(ej::render_note(make_template("Played {value}"), "") == "Played ");
}

TEST_CASE("render_note is total on an invalid pattern", "[note-template]")
{
    CHECK(ej::render_note(make_template("{name} {value} }"), "v") == "{name} v }");
}
