# SPDX-License-Identifier: GPL-3.0-or-later

# The version is written as constants in a header; code reads them with `if constexpr`
# or as plain values. CMake never defines a macro that changes which code compiles.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/icl_template/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/icl_template/version.hpp"
    @ONLY)

add_library(icl_template_core
    src/greeting.cpp
    src/version.cpp)
add_library(icl_template::core ALIAS icl_template_core)

# One implementation file per platform; CMake adds the directory that matches the target.
icl_platform_sources(icl_template_core)

target_include_directories(icl_template_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
target_compile_features(icl_template_core PUBLIC cxx_std_23)
target_link_libraries(icl_template_core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)

if(ICL_TEMPLATE_BUILD_TESTS)
    add_subdirectory(tests)
endif()
