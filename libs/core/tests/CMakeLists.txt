# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(icl_template_core_tests
    test_greeting.cpp
    test_parse.cpp
    test_platform.cpp)
target_link_libraries(icl_template_core_tests
    PRIVATE icl_template::core Catch2::Catch2WithMain icl::warnings icl::coverage)

# Every Catch2 case becomes a CTest test carrying the `unit` label (the labels are
# unit, integration, system, ui, perf and fuzz).
catch_discover_tests(icl_template_core_tests PROPERTIES LABELS unit)
