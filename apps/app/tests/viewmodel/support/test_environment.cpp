// SPDX-License-Identifier: GPL-3.0-or-later
#include "support/test_environment.hpp"

#include "golden.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <map>
#include <string>

namespace ej::test
{

Configuration sample_configuration()
{
    const auto text = read_bytes(std::filesystem::path(EJ_SAMPLE_CONFIG));
    REQUIRE(text.has_value());
    auto imported = import_configuration(*text);
    REQUIRE(imported.has_value());
    return std::move(*imported);
}

FakeTimeZoneProvider make_fixed_zones()
{
    using namespace std::chrono_literals;
    return FakeTimeZoneProvider(std::map<std::string, std::chrono::minutes, std::less<>>{
        {"Australia/Melbourne", 11h}, {"Australia/Perth", 8h}, {"UTC", 0h}});
}

} // namespace ej::test
