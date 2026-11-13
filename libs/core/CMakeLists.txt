# SPDX-License-Identifier: GPL-3.0-or-later

# The version is written as constants in a header; code reads them with `if constexpr`
# or as plain values. CMake never defines a macro that changes which code compiles.
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/error_journaling/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/error_journaling/version.hpp"
    @ONLY)

# QtCore only (docs/architecture.md): no GUI, QML or SQL here, so the library builds and tests without a display.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)

add_library(ej_core
    src/clock.cpp
    src/configuration.cpp
    src/day_prompt.cpp
    src/export_renderer.cpp
    src/extra_versions.cpp
    src/header_history.cpp
    src/header_values.cpp
    src/iso_date.cpp
    src/journal_record.cpp
    src/log.cpp
    src/note_template.cpp
    src/qt_time_zone_provider.cpp
    src/recovery_report.cpp
    src/software_version.cpp
    src/storage_error.cpp
    src/stored_log.cpp
    src/time_format.cpp
    src/unit_number.cpp
    src/utf8.cpp
    src/uuid.cpp
    src/version.cpp)
add_library(error_journaling::core ALIAS ej_core)

target_include_directories(ej_core
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/generated>")
target_compile_features(ej_core PUBLIC cxx_std_23)
target_link_libraries(ej_core
    PUBLIC Qt6::Core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)

if(ERROR_JOURNALING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
