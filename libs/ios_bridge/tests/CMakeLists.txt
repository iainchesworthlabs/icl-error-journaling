# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_ios_bridge_tests
    test_ios_bridge.cpp)
target_link_libraries(ej_ios_bridge_tests
    PRIVATE ej_ios_bridge Catch2::Catch2WithMain icl::warnings icl::coverage)

catch_discover_tests(ej_ios_bridge_tests PROPERTIES LABELS unit)
