# SPDX-License-Identifier: GPL-3.0-or-later

# `unit`: no files and no database. The in-memory repository through the contract suite, and the small pieces.
add_executable(ej_storage_tests
    test_contract_memory.cpp
    test_crash_point.cpp
    test_storage.cpp)
target_link_libraries(ej_storage_tests
    PRIVATE ej_storage Catch2::Catch2WithMain icl::warnings icl::coverage)
# storage_samples.hpp: the sample data the core tests of the journal format share with these.
target_include_directories(ej_storage_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/core/tests")
catch_discover_tests(ej_storage_tests PROPERTIES LABELS unit)

# `integration`: real SQLite and real files, in a directory of the test's own under the build tree.
find_package(Qt6 6.8 REQUIRED COMPONENTS Sql)
add_executable(ej_storage_integration_tests
    test_main.cpp
    test_contract_sqlite.cpp
    test_sqlite_repository.cpp
    test_storage_location.cpp)
target_link_libraries(ej_storage_integration_tests
    PRIVATE ej_storage Qt6::Sql Catch2::Catch2 icl::warnings icl::coverage)
target_include_directories(ej_storage_integration_tests PRIVATE "${PROJECT_SOURCE_DIR}/libs/core/tests")
catch_discover_tests(ej_storage_integration_tests PROPERTIES LABELS integration)
