# SPDX-License-Identifier: GPL-3.0-or-later
cmake_minimum_required(VERSION 4.0)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake/icl" "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Chooses the vcpkg checkout for this configure. Must run before project().
include(IclVcpkgRoot)
icl_resolve_vcpkg("${CMAKE_CURRENT_SOURCE_DIR}" ERROR_JOURNALING)

project(error_journaling
    VERSION 0.1.0
    DESCRIPTION "An iPhone app for logging robot session errors through the day and sharing the log"
    HOMEPAGE_URL "https://github.com/iainchesworthlabs/icl-error-journaling"
    LANGUAGES CXX)

set(ICL_PROJECT_PREFIX ERROR_JOURNALING)

# The application id on Android and the bundle identifier on iOS: com.iainchesworthlabs.<app>, the App ID registered with
# Apple and what the provisioning profile names. Defined once; the iOS workflows repeat it as BUNDLE_ID and
# apps/app/packaging/ios/verify_app.sh checks that the two agree.
set(ERROR_JOURNALING_APPLICATION_ID "com.iainchesworthlabs.errorjournaling")
set(ERROR_JOURNALING_DISPLAY_NAME "CS Bingo Board")

# The iOS bridge is Objective-C++ (libs/ios_bridge/src/platform/ios).
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
include(IclQtApp)

option(ERROR_JOURNALING_BUILD_TESTS "Build the tests" ON)
option(ERROR_JOURNALING_BUILD_APP "Build the Qt Quick application (the iPhone application on iOS, a desktop program elsewhere)" ON)

if(ERROR_JOURNALING_BUILD_TESTS)
    include(CTest)
    find_package(Catch2 3 CONFIG REQUIRED)
    include(Catch)
endif()

add_subdirectory(libs/core)
add_subdirectory(libs/storage)
add_subdirectory(libs/ios_bridge)

if(ERROR_JOURNALING_BUILD_APP)
    add_subdirectory(apps/app)
endif()

# The desktop tool that makes the one-year dataset for the device measurements (Stage 5, M1). Not built for a device.
if(ERROR_JOURNALING_BUILD_TESTS AND NOT IOS)
    add_subdirectory(tools/dataset)
endif()
