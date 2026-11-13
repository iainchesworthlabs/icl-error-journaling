// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "error_journaling/result.hpp"
#include "error_journaling/software_version.hpp"

namespace ej
{

/// A software version with a name: "Influencer" and 4.5.0. The robot's own version is the header's software version and
/// has no label of its own in the data; the screens call it "Robot".
struct LabelledVersion
{
    std::string m_Label;
    SoftwareVersion m_Version;

    friend bool operator==(const LabelledVersion&, const LabelledVersion&) = default;
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

inline constexpr std::size_t MAX_EXTRA_LABEL_LENGTH = 20;

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
