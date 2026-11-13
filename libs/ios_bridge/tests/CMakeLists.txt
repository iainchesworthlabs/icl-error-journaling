# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_ios_bridge_tests
    test_dictation.cpp
    test_ios_bridge.cpp)
target_link_libraries(ej_ios_bridge_tests
    PRIVATE ej_qml_bridge ej_ios_bridge Qt6::Core Qt6::Gui Catch2::Catch2WithMain icl::warnings icl::coverage)
# The PRD example of the export is the byte-for-byte reference for what reaches the share sheet; the sample setup is the
# fictional file the importer tests use.
target_compile_definitions(ej_ios_bridge_tests
    PRIVATE EJ_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../core/tests/golden"
            EJ_SAMPLE_CONFIG="${PROJECT_SOURCE_DIR}/config/sample-config.json")

# The tags in the test names choose the label: [integration] uses real files, everything else is a fake.
ej_discover_tests(ej_ios_bridge_tests TEST_SPEC "~[integration]" PROPERTIES LABELS unit)
ej_discover_tests(ej_ios_bridge_tests TEST_SPEC "[integration]" TEST_SUFFIX " (integration)" PROPERTIES LABELS integration)
