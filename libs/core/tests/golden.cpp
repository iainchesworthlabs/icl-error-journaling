// SPDX-License-Identifier: GPL-3.0-or-later
#include "golden.hpp"

#include <catch2/catch_test_macros.hpp>

#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

namespace ej::test
{

namespace
{

std::vector<std::string_view> split_lines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    while (true)
    {
        const auto end = text.find('\n', start);
        if (end == std::string_view::npos)
        {
            lines.push_back(text.substr(start));
            break;
        }
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

}  // namespace

std::optional<std::string> read_bytes(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::optional<std::string> first_difference(std::string_view expected, std::string_view actual)
{
    if (expected == actual)
    {
        return std::nullopt;
    }
    const auto e = split_lines(expected);
    const auto a = split_lines(actual);
    for (std::size_t i = 0; i < e.size() || i < a.size(); ++i)
    {
        const bool has_e = i < e.size();
        const bool has_a = i < a.size();
        if (has_e && has_a && e[i] == a[i])
        {
            continue;
        }
        std::ostringstream message;
        message << "line " << (i + 1) << ": expected ";
        if (has_e)
        {
            message << '"' << e[i] << '"';
        }
        else
        {
            message << "end of text";
        }
        message << ", actual ";
        if (has_a)
        {
            message << '"' << a[i] << '"';
        }
        else
        {
            message << "end of text";
        }
        return message.str();
    }
    return "texts differ";
}

std::filesystem::path fixture_path(std::string_view name)
{
    return std::filesystem::path(EJ_FIXTURE_DIR) / std::string(name);
}

std::optional<std::string> compare_golden(std::string_view name, std::string_view actual)
{
    const auto expected_path = std::filesystem::path(EJ_GOLDEN_DIR) / (std::string(name) + ".txt");
    const auto expected = read_bytes(expected_path);
    const auto difference = expected ? first_difference(*expected, actual) : std::optional<std::string>("golden file not found");
    if (!difference)
    {
        return std::nullopt;
    }
    const auto actual_dir = std::filesystem::path(EJ_GOLDEN_ACTUAL_DIR);
    std::filesystem::create_directories(actual_dir);
    const auto actual_path = actual_dir / (std::string(name) + ".actual.txt");
    {
        std::ofstream out(actual_path, std::ios::binary);
        out.write(actual.data(), static_cast<std::streamsize>(actual.size()));
    }
    return "golden " + std::string(name) + ": " + *difference + "; actual output written to " + actual_path.string();
}

void check_golden(std::string_view name, std::string_view actual)
{
    const auto result = compare_golden(name, actual);
    INFO(result.value_or(std::string{}));
    CHECK_FALSE(result.has_value());
}

}  // namespace ej::test
