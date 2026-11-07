# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_storage_tests
    test_storage.cpp)
target_link_libraries(ej_storage_tests
    PRIVATE ej_storage Catch2::Catch2WithMain icl::warnings icl::coverage)

catch_discover_tests(ej_storage_tests PROPERTIES LABELS unit)
