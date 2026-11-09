// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "error_journaling/result.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace ej
{

/// major.minor.patch (HDR-04): three components of one to nine ASCII digits, no leading zeros (a lone 0 is fine). It
/// may carry the commit of the build (HDR-09): 7 to 12 hexadecimal characters, kept in lower case. The commit is
/// optional; "1.4.2" is a version.
///
/// The stored text of a version is the three components, then a plus sign and the commit when there is one:
/// "1.4.2" or "1.4.2+a1b2c3d". A plus sign is not part of a component, so the first one ends the version. Text stored
/// before commits existed has no plus sign and reads as a version without a commit. The commit is not a part of what
/// the person types in the version field: `parse(text)` takes the components alone.
class SoftwareVersion
{
public:
    enum class Error
    {
        EMPTY,
        WRONG_COMPONENT_COUNT,
        NOT_DIGITS,
        LEADING_ZERO,
        TOO_LONG,
        COMMIT_NOT_HEX, // a commit has a character that is not 0 to 9 or a to f (either case)
        COMMIT_LENGTH   // a commit that is not 7 to 12 characters, or 40 (a full hash, which is shortened)
    };

    static constexpr std::size_t MIN_COMMIT_LENGTH = 7;
    static constexpr std::size_t MAX_COMMIT_LENGTH = 12;
    /// A full SHA-1 hash, as git prints it. It is shortened to `MIN_COMMIT_LENGTH` characters.
    static constexpr std::size_t FULL_COMMIT_LENGTH = 40;

    /// The three components alone: no commit, and a plus sign is not a digit.
    [[nodiscard]] static Result<SoftwareVersion, Error> parse(std::string_view text);

    /// The components and a commit as typed; an empty `commit` means none. The commit is lower-cased, and a full
    /// 40-character hash is shortened to its first 7 characters.
    [[nodiscard]] static Result<SoftwareVersion, Error> parse(std::string_view text, std::string_view commit);

    /// The stored text: "1.4.2" or "1.4.2+a1b2c3d". The commit is normalised as `normalize_commit` does.
    [[nodiscard]] static Result<SoftwareVersion, Error> parse_stored(std::string_view text);

    /// A commit as it is kept: "" for none, otherwise 7 to 12 lower-case hexadecimal characters. Upper case is
    /// lower-cased and a 40-character hash is shortened to 7. Anything else is refused.
    [[nodiscard]] static Result<std::string, Error> normalize_commit(std::string_view commit);

    /// The components alone: "1.4.2".
    [[nodiscard]] std::string format() const;
    /// Empty when the version has no commit.
    [[nodiscard]] std::string_view commit() const
    {
        return m_Commit;
    }
    /// What is stored, and what `parse_stored` reads back: "1.4.2" or "1.4.2+a1b2c3d".
    [[nodiscard]] std::string stored() const;
    /// What the log prints: "1.4.2" or "1.4.2 (a1b2c3d)".
    [[nodiscard]] std::string display() const;

    friend auto operator<=>(const SoftwareVersion&, const SoftwareVersion&) = default;

private:
    SoftwareVersion(std::uint32_t major, std::uint32_t minor, std::uint32_t patch, std::string commit)
        : m_Major(major), m_Minor(minor), m_Patch(patch), m_Commit(std::move(commit))
    {
    }

    std::uint32_t m_Major = 0;
    std::uint32_t m_Minor = 0;
    std::uint32_t m_Patch = 0;
    std::string m_Commit;
};

}  // namespace ej
