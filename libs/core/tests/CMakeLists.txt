# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_core_tests
    test_version.cpp)
target_link_libraries(ej_core_tests
    PRIVATE ej_core Catch2::Catch2WithMain icl::warnings icl::coverage)

# Every Catch2 case becomes a CTest test carrying the `unit` label (the labels are
# unit, integration, system, ui, perf and fuzz).
catch_discover_tests(ej_core_tests PROPERTIES LABELS unit)
