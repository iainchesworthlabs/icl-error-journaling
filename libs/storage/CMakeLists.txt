# SPDX-License-Identifier: GPL-3.0-or-later
# The log's storage (Stage 2): the in-memory and SQLite repositories. The journal files and recovery follow in later work
# packages of docs/plan/stage-2.md.

# Qt SQL for the SQLite repository. The public headers name no Qt SQL type, so the library's users link QtCore only.
find_package(Qt6 6.8 REQUIRED COMPONENTS Sql)

add_library(ej_storage
    src/crash_point.cpp
    src/memory_log_repository.cpp
    src/sqlite_log_repository.cpp
    src/storage.cpp
    src/storage_location.cpp)
add_library(error_journaling::storage ALIAS ej_storage)

target_include_directories(ej_storage PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(ej_storage PUBLIC cxx_std_23)
target_link_libraries(ej_storage
    PUBLIC ej_core
    PRIVATE Qt6::Sql icl::warnings icl::hardening icl::tracy icl::coverage)

if(ERROR_JOURNALING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
