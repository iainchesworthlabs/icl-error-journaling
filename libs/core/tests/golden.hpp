// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace ej::test
{

/// Reads a file as bytes (no newline translation). Empty optional if it cannot be opened.
[[nodiscard]] std::optional<std::string> read_bytes(const std::filesystem::path& path);

/// Describes the first difference between two texts, line by line ("line 3: expected ..., actual ..."), or an empty
/// optional when they are byte-identical. Differences in the final newline are reported too.
[[nodiscard]] std::optional<std::string> first_difference(std::string_view expected, std::string_view actual);

/// Compares `actual` with `golden/<name>.txt`. On a mismatch it writes `<name>.actual.txt` into the build directory
/// (EJ_GOLDEN_ACTUAL_DIR) and returns the description; on a match it returns an empty optional.
[[nodiscard]] std::optional<std::string> compare_golden(std::string_view name, std::string_view actual);

/// Path of a fixture under `libs/core/tests/fixtures/`.
[[nodiscard]] std::filesystem::path fixture_path(std::string_view name);

}  // namespace ej::test

/// Use inside a test case: `CHECK_GOLDEN("prd-example", text);`
#define CHECK_GOLDEN(name, actual)                                                                                    \
    do                                                                                                                \
    {                                                                                                                 \
        const auto ej_golden_result = ::ej::test::compare_golden((name), (actual));                                   \
        INFO(ej_golden_result.value_or(std::string{}));                                                               \
        CHECK_FALSE(ej_golden_result.has_value());                                                                    \
    } while (false)
