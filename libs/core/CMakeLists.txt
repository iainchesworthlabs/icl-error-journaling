# SPDX-License-Identifier: GPL-3.0-or-later

# The version is written as constants in a header; code reads them with `if constexpr`
# or as plain values. CMake never defines a macro that changes which code compiles.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/error_journaling/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/error_journaling/version.hpp"
    @ONLY)

add_library(error_journaling_core
    src/greeting.cpp
    src/version.cpp)
add_library(error_journaling::core ALIAS error_journaling_core)

# One implementation file per platform; CMake adds the directory that matches the target.
icl_platform_sources(error_journaling_core)

target_include_directories(error_journaling_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
target_compile_features(error_journaling_core PUBLIC cxx_std_23)
target_link_libraries(error_journaling_core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)

if(ERROR_JOURNALING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
