# SPDX-License-Identifier: GPL-3.0-or-later
# The log's storage (Stage 2). The in-memory repository now; SQLite, the journal and recovery follow in later work
# packages of docs/plan/stage-2.md.

add_library(ej_storage
    src/memory_log_repository.cpp
    src/storage.cpp)
add_library(error_journaling::storage ALIAS ej_storage)

target_include_directories(ej_storage PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(ej_storage PUBLIC cxx_std_23)
target_link_libraries(ej_storage
    PUBLIC ej_core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)

if(ERROR_JOURNALING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
