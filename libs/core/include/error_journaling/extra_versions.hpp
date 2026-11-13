// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/result.hpp"
#include "error_journaling/software_version.hpp"

namespace ej
{

inline constexpr std::size_t MAX_EXTRA_LABEL_LENGTH = 20;

/// A software version with a name: "Influencer" and 4.5.0. The robot's own version is the header's software version and
/// has no label of its own in the data; the screens call it "Robot".
///
/// The label is kept in the object, as `SoftwareVersion` keeps its commit, and not in a string: a header is copied and
/// destroyed inside the journal record variants, and GCC's analysis of those reports a string held there as possibly
/// uninitialised (a false warning that stops the build; see journal_seed.cpp). A label is at most
/// `MAX_EXTRA_LABEL_LENGTH` characters; a longer one is cut, so callers check it with `normalize_label` first.
class LabelledVersion
{
public:
    LabelledVersion(std::string_view label, SoftwareVersion version) : m_Version(version)
    {
        m_LabelLength = static_cast<std::uint8_t>(std::min(label.size(), m_LabelChars.size()));
        std::copy_n(label.begin(), m_LabelLength, m_LabelChars.begin());
    }

    [[nodiscard]] std::string_view label() const
    {
        return {m_LabelChars.data(), m_LabelLength};
    }

    SoftwareVersion m_Version;

    friend bool operator==(const LabelledVersion& a, const LabelledVersion& b)
    {
        return a.label() == b.label() && a.m_Version == b.m_Version;
    }

private:
    std::array<char, 20> m_LabelChars{};
    std::uint8_t m_LabelLength = 0;
};

/// The other software versions of a log (HDR-10), in the order the person gave them.
using ExtraVersions = std::vector<LabelledVersion>;

enum class ExtraVersionsError
{
    EMPTY_LABEL,
    LABEL_TOO_LONG,       // more than `MAX_EXTRA_LABEL_LENGTH` characters
    LABEL_CHARACTERS,     // anything but letters and digits (ASCII), spaces and dashes
    LABEL_IS_ROBOT,       // "Robot" in any case: that name belongs to the header's software version
    DUPLICATE_LABEL,      // the same label (in any case) twice
    INVALID_VERSION,      // a version that `SoftwareVersion` refuses
    MALFORMED             // stored text with a line that is not label=version
};


/// The name the screens and the log give to the header's own software version.
inline constexpr std::string_view ROBOT_LABEL = "Robot";

/// The stored text when there are no extra versions. A correction needs a value, and "" is not one.
inline constexpr std::string_view NO_EXTRA_VERSIONS = "none";

/// A label as it is kept: trimmed, inner runs of spaces collapsed to one. Refused when empty, too long, with a
/// character other than ASCII letters, digits, space and dash, or when it is "Robot" in any case.
[[nodiscard]] Result<std::string, ExtraVersionsError> normalize_label(std::string_view label);

/// Checks the whole list: every label as `normalize_label` does, and no label twice without regard to case.
[[nodiscard]] Result<void, ExtraVersionsError> check_extra_versions(const ExtraVersions& versions);

/// The stored text: one "label=1.4.2" (with "+commit" when it has one) per line, or `NO_EXTRA_VERSIONS`.
[[nodiscard]] std::string stored_extra_versions(const ExtraVersions& versions);

/// Reads the stored text back, checking it as `check_extra_versions` does.
[[nodiscard]] Result<ExtraVersions, ExtraVersionsError> parse_extra_versions(std::string_view stored);

/// What the log prints for one version: "Influencer Version" is the line label, and `display()` the value.
[[nodiscard]] std::string extra_version_line_label(const LabelledVersion& version);

/// A short text for a list of them: "Influencer v4.5.0 · Gateway v2.0.1", or "" for none.
[[nodiscard]] std::string summarize_extra_versions(const ExtraVersions& versions);

}  // namespace ej
