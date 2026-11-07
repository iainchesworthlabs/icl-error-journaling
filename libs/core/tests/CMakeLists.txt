# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(error_journaling_core_tests
    test_greeting.cpp
    test_parse.cpp
    test_platform.cpp)
target_link_libraries(error_journaling_core_tests
    PRIVATE error_journaling::core Catch2::Catch2WithMain icl::warnings icl::coverage)

# Every Catch2 case becomes a CTest test carrying the `unit` label (the labels are
# unit, integration, system, ui, perf and fuzz).
catch_discover_tests(error_journaling_core_tests PROPERTIES LABELS unit)
