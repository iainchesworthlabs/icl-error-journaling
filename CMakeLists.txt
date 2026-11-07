# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 4.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/icl" "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Chooses the vcpkg checkout for this configure. Must run before project().
include(IclVcpkgRoot)
icl_resolve_vcpkg("${CMAKE_CURRENT_SOURCE_DIR}" ICL_TEMPLATE)

project(icl_template
    VERSION 0.1.0
    DESCRIPTION "Template for new C++23 projects"
    HOMEPAGE_URL "https://github.com/iainchesworthlabs/icl-cpp-template"
    LANGUAGES CXX)

set(ICL_PROJECT_PREFIX ICL_TEMPLATE)

# The application id on Android and the bundle identifier on iOS: com.iainchesworthlabs.<app>, the App ID registered with
# Apple and what the provisioning profile names. Defined once; the iOS workflows repeat it as BUNDLE_ID and
# apps/ios/packaging/verify_app.sh checks that the two agree.
set(ICL_TEMPLATE_APPLICATION_ID "com.iainchesworthlabs.icltemplate")
set(ICL_TEMPLATE_DISPLAY_NAME "ICL Template")

# The iOS application's code is Objective-C++ (apps/ios).
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sanitizers apply to every target, so they are configured first.
include(IclSanitizers)
include(IclWarnings)
include(IclHardening)
include(IclTracy)
include(IclCoverage)
include(IclPlatform)

option(ICL_TEMPLATE_BUILD_TESTS "Build the tests" ON)
option(ICL_TEMPLATE_BUILD_CLI "Build the command-line application" ON)
option(ICL_TEMPLATE_BUILD_IOS_APP "Build the iPhone and iPad application (iOS only)" ON)

if(ICL_TEMPLATE_BUILD_TESTS)
    include(CTest)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(libs/core)

if(ICL_TEMPLATE_BUILD_CLI)
    add_subdirectory(apps/cli)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "iOS" AND ICL_TEMPLATE_BUILD_IOS_APP)
    add_subdirectory(apps/ios)
endif()
