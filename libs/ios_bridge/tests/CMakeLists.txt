# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_ios_bridge_tests
    test_ios_bridge.cpp)
target_link_libraries(ej_ios_bridge_tests
    PRIVATE ej_qml_bridge ej_ios_bridge Qt6::Core Catch2::Catch2WithMain icl::warnings icl::coverage)
# The PRD example of the export is the byte-for-byte reference for what reaches the share sheet.
target_compile_definitions(ej_ios_bridge_tests
    PRIVATE EJ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/golden")

# The tags in the test names choose the label: [integration] uses real files, everything else is a fake.
catch_discover_tests(ej_ios_bridge_tests TEST_SPEC "~[integration]" PROPERTIES LABELS unit)
catch_discover_tests(ej_ios_bridge_tests TEST_SPEC "[integration]" TEST_SUFFIX " (integration)" PROPERTIES LABELS integration)
