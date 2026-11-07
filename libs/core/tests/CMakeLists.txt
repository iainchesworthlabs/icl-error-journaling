# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_core_tests
    golden.cpp
    golden_scenarios.cpp
    test_day_prompt.cpp
    test_export_skeleton.cpp
    test_foundations.cpp
    test_log.cpp
    test_time_format.cpp
    test_time_zone.cpp
    test_uuid.cpp
    test_validators.cpp
    test_version.cpp)
target_link_libraries(ej_core_tests
    PRIVATE ej_core Catch2::Catch2WithMain icl::warnings icl::coverage)

target_compile_definitions(ej_core_tests
    PRIVATE
        EJ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        EJ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        EJ_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

# Every Catch2 case becomes a CTest test carrying the `unit` label (the labels are
# unit, integration, system, ui, perf and fuzz).
catch_discover_tests(ej_core_tests PROPERTIES LABELS unit)
