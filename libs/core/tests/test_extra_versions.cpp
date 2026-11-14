// SPDX-License-Identifier: GPL-3.0-or-later
//
// The versions besides the robot's (HDR-10): the labels, the stored text, the journal, the history and the export. Also
// the "v" of a version (HDR-04): typed or not, printed once.
#include "error_journaling/export_renderer.hpp"
#include "error_journaling/extra_versions.hpp"
#include "error_journaling/header_history.hpp"
#include "error_journaling/journal_record.hpp"
#include "storage_samples.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace ej;
using namespace ej::test;

namespace
{

LabelledVersion labelled(const char* label, const char* version)
{
    return LabelledVersion{label, SoftwareVersion::parse(version).value()};
}

HeaderValues bare_header()
{
    HeaderValues header;
    header.m_Tester = "Sam Example";
    header.m_LocalDate = std::chrono::year{2026} / 11 / 7;
    return header;
}

ExportContext demo_context()
{
    ExportContext context;
    context.m_UnitPrefix = "UNIT";
    context.m_LocationName = [](LocationId) { return std::string_view("North Site"); };
    return context;
}

}  // namespace

TEST_CASE("A leading v on a version is accepted and never kept", "[software-version][extra-versions]")
{
    const auto plain = SoftwareVersion::parse("1.4.2").value();
    CHECK(SoftwareVersion::parse("v1.4.2").value() == plain);
    CHECK(SoftwareVersion::parse("V1.4.2").value() == plain);
    CHECK(SoftwareVersion::parse("v1.4.2", "a1b2c3d").value().stored() == "1.4.2+a1b2c3d");
    CHECK(SoftwareVersion::parse_stored("v1.4.2+a1b2c3d").value().stored() == "1.4.2+a1b2c3d");
    CHECK(SoftwareVersion::parse("v1.4.2").value().display() == "v1.4.2");
    // Only one, and only in front.
    CHECK_FALSE(SoftwareVersion::parse("vv1.4.2").has_value());
    CHECK_FALSE(SoftwareVersion::parse("1.4.2v").has_value());
    CHECK(SoftwareVersion::parse("v").error() == SoftwareVersion::Error::EMPTY);
}

TEST_CASE("A label is trimmed, collapsed and checked", "[extra-versions]")
{
    CHECK(normalize_label("  Influencer ").value() == "Influencer");
    CHECK(normalize_label("Phone   app-2").value() == "Phone app-2");
    CHECK(normalize_label("").error() == ExtraVersionsError::EMPTY_LABEL);
    CHECK(normalize_label("   ").error() == ExtraVersionsError::EMPTY_LABEL);
    CHECK(normalize_label("12345678901234567890").has_value());
    CHECK(normalize_label("123456789012345678901").error() == ExtraVersionsError::LABEL_TOO_LONG);
    CHECK(normalize_label("a=b").error() == ExtraVersionsError::LABEL_CHARACTERS);
    CHECK(normalize_label("a\nb").error() == ExtraVersionsError::LABEL_CHARACTERS);
    CHECK(normalize_label("Influencer\xC3\xA9").error() == ExtraVersionsError::LABEL_CHARACTERS);
    CHECK(normalize_label("robot").error() == ExtraVersionsError::LABEL_IS_ROBOT);
    CHECK(normalize_label("ROBOT").error() == ExtraVersionsError::LABEL_IS_ROBOT);
}

TEST_CASE("The list is stored one label=version per line and reads back", "[extra-versions]")
{
    ExtraVersions list{labelled("Influencer", "4.5.0"), labelled("Gateway", "2.0.1")};
    list[1].m_Version = SoftwareVersion::parse("2.0.1", "a1b2c3d").value();
    const std::string stored = stored_extra_versions(list);
    CHECK(stored == "Influencer=4.5.0\nGateway=2.0.1+a1b2c3d");
    CHECK(parse_extra_versions(stored).value() == list);

    CHECK(stored_extra_versions({}) == "none");
    CHECK(parse_extra_versions("none").value().empty());
    CHECK(summarize_extra_versions(list) == "Influencer v4.5.0 \xC2\xB7 Gateway v2.0.1 (a1b2c3d)");
    CHECK(summarize_extra_versions({}).empty());
    CHECK(extra_version_line_label(list[0]) == "Influencer Version");
}

TEST_CASE("Stored lists that are wrong are refused", "[extra-versions]")
{
    CHECK(parse_extra_versions("").error() == ExtraVersionsError::MALFORMED);
    CHECK(parse_extra_versions("Influencer").error() == ExtraVersionsError::MALFORMED);
    CHECK(parse_extra_versions("Influencer=4.5").error() == ExtraVersionsError::INVALID_VERSION);
    CHECK(parse_extra_versions("Robot=1.0.0").error() == ExtraVersionsError::LABEL_IS_ROBOT);
    CHECK(parse_extra_versions("A=1.0.0\na=2.0.0").error() == ExtraVersionsError::DUPLICATE_LABEL);
    CHECK(check_extra_versions({labelled(" A", "1.0.0")}).error() == ExtraVersionsError::LABEL_CHARACTERS);
}

TEST_CASE("The header field takes the list, with none as a value of its own", "[extra-versions][header]")
{
    HeaderValues header = sample_header();
    CHECK(header_field_text(header, HeaderField::EXTRA_VERSIONS) == "none");
    REQUIRE(set_header_field(header, HeaderField::EXTRA_VERSIONS, "Influencer=v4.5.0").has_value());
    CHECK(header.m_ExtraVersions.size() == 1);
    CHECK(header_field_text(header, HeaderField::EXTRA_VERSIONS) == "Influencer=4.5.0");
    const auto refused = set_header_field(header, HeaderField::EXTRA_VERSIONS, "Robot=1.0.0");
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().m_Kind == HeaderError::Kind::INVALID_EXTRA_VERSIONS);
    REQUIRE(set_header_field(header, HeaderField::EXTRA_VERSIONS, "none").has_value());
    CHECK(header.m_ExtraVersions.empty());
    CHECK(header_field_from_key("extra_versions") == HeaderField::EXTRA_VERSIONS);
}

TEST_CASE("A log without extra versions is written as before, and one with them reads back", "[journal][extra-versions]")
{
    const auto line_of = [](const HeaderValues& header)
    {
        const auto line = encode_record(sample_record(1, CreateLogBody{header, sample_time(0), std::nullopt}));
        REQUIRE(line.has_value());
        return *line;
    };
    const std::string plain = line_of(sample_header());
    CHECK(plain.find("extra_versions") == std::string::npos);

    HeaderValues header = sample_header();
    header.m_ExtraVersions = {labelled("Influencer", "4.5.0")};
    const std::string line = line_of(header);
    CHECK(line.find("\"extra_versions\":\"Influencer=4.5.0\"") != std::string::npos);
    const auto decoded = decode_record(line);
    REQUIRE(decoded.has_value());
    CHECK(std::get<CreateLogBody>(decoded->m_Body).m_Header.m_ExtraVersions == header.m_ExtraVersions);
    CHECK(*encode_record(*decoded) == line);

    // A line written before the key existed has none.
    const auto old = decode_record(plain);
    REQUIRE(old.has_value());
    CHECK(std::get<CreateLogBody>(old->m_Body).m_Header.m_ExtraVersions.empty());

    // A list that is not valid is a bad field and not a lost header.
    std::string damaged = line;
    damaged.replace(damaged.find("Influencer=4.5.0"), 16, "Influencer=4.5");
    const auto bad = decode_record(damaged);
    REQUIRE_FALSE(bad.has_value());
}

TEST_CASE("The export prints a line for each extra version after the software version", "[export][extra-versions]")
{
    HeaderValues values = bare_header();
    values.m_SoftwareVersion = SoftwareVersion::parse("1.2.3").value();
    CHECK(render_header(Log(values), demo_context()).value() ==
          "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #:\nLocation:\nSoftware Version: v1.2.3");

    values.m_ExtraVersions = {labelled("Influencer", "4.5.0"), labelled("Gateway", "2.0.1")};
    CHECK(render_header(Log(values), demo_context()).value() ==
          "Facilitator: Sam Example\nDate: 7 Nov 2026\nUNIT #:\nLocation:\nSoftware Version: v1.2.3\n"
          "Influencer Version: v4.5.0\nGateway Version: v2.0.1");
}

TEST_CASE("A stored label that is too long is refused and not cut", "[extra-versions]")
{
    CHECK(parse_extra_versions("123456789012345678901=1.0.0").error() == ExtraVersionsError::LABEL_TOO_LONG);
    CHECK(parse_extra_versions("12345678901234567890=1.0.0").value().at(0).label() == "12345678901234567890");
}
