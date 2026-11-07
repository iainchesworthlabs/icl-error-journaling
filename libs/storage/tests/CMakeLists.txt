# SPDX-License-Identifier: GPL-3.0-or-later
add_executable(ej_storage_tests
    test_repository_contract.cpp
    test_storage.cpp)
target_link_libraries(ej_storage_tests
    PRIVATE ej_storage Catch2::Catch2WithMain icl::warnings icl::coverage)
# storage_samples.hpp: the sample data the core tests of the journal format share with these.
target_include_directories(ej_storage_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/core/tests")

catch_discover_tests(ej_storage_tests PROPERTIES LABELS unit)
