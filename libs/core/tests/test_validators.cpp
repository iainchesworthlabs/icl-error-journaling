// SPDX-License-Identifier: GPL-3.0-or-later
#include "error_journaling/note_template.hpp"
#include "error_journaling/software_version.hpp"
#include "error_journaling/unit_number.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

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
    for (const char* text : {"1.4.2-beta", "1.4.2+3", "v1.4.2", "1..2", "1.4.", ".4.2", " 1.4.2"})
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
