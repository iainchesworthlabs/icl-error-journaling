# SPDX-License-Identifier: GPL-3.0-or-later
# The golden comparison, the scenarios behind the golden files and the fixed-offset zone provider, as a library so that the
# view-model tests (apps/app/tests/viewmodel) compare against the same golden files, not copies. The golden and fixture
# paths are compiled into it; a mismatch writes its `<name>.actual.txt` to golden-actual in this directory's build tree.
add_library(ej_core_test_support STATIC
    golden.cpp
    golden.hpp
    golden_scenarios.cpp
    golden_scenarios.hpp
    fake_time_zone_provider.hpp
    storage_samples.hpp)
target_include_directories(ej_core_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ej_core_test_support
    PRIVATE
        EJ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        EJ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        EJ_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")
target_link_libraries(ej_core_test_support
    PUBLIC ej_core Catch2::Catch2
    PRIVATE icl::warnings icl::coverage)

add_executable(ej_core_tests
    test_configuration.cpp
    test_clock.cpp
    test_day_prompt.cpp
    test_export_header.cpp
    test_export_golden.cpp
    test_export_rules.cpp
    test_extra_versions.cpp
    test_export_snapshot.cpp
    test_foundations.cpp
    test_header_history.cpp
    test_iso_date.cpp
    test_journal_record.cpp
    test_log.cpp
    test_recovery_report.cpp
    test_storage_error.cpp
    test_stored_log.cpp
    test_time_format.cpp
    test_time_zone.cpp
    test_utf8.cpp
    test_uuid.cpp
    test_validators.cpp
    test_version.cpp)
target_link_libraries(ej_core_tests
    PRIVATE ej_core ej_core_test_support Catch2::Catch2WithMain icl::warnings icl::coverage)

target_compile_definitions(ej_core_tests
    PRIVATE
        EJ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
        EJ_SAMPLE_CONFIG="${PROJECT_SOURCE_DIR}/config/sample-config.json"
        EJ_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
        EJ_GOLDEN_ACTUAL_DIR="${CMAKE_CURRENT_BINARY_DIR}/golden-actual")

# Every Catch2 case becomes a CTest test carrying the `unit` label (the labels are
# unit, integration, system, ui, perf and fuzz).
ej_discover_tests(ej_core_tests PROPERTIES LABELS unit)
