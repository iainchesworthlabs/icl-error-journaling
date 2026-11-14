# SPDX-License-Identifier: GPL-3.0-or-later
# The log's storage (Stage 2): the in-memory and SQLite repositories and the journal files. Recovery and migrations follow in
# later work packages of docs/plan/stage-2.md.

# Qt SQL for the SQLite repository. The public headers name no Qt SQL type, so the library's users link QtCore only.
find_package(Qt6 6.8 REQUIRED COMPONENTS Sql)

add_library(ej_storage
    src/crash_point.cpp
    src/draft_store.cpp
    src/file_journal.cpp
    src/journal_file.cpp
    src/journal_reader.cpp
    src/journal_seed.cpp
    src/journal_writer.cpp
    src/memory_log_repository.cpp
    src/migrator.cpp
    src/people_journal.cpp
    src/people_store.cpp
    src/platform/android_protection.cpp
    src/protect.cpp
    src/quarantine.cpp
    src/recovery.cpp
    src/sqlite_log_repository.cpp
    src/storage.cpp
    src/storage_location.cpp)
# GCC 16 reports a string of the Entry copy in journal_seed.cpp as possibly uninitialised (the cleanup path of the variant's
# copy constructor, which cannot run with an uninitialised member). It came and went with the size of the header in the
# record variant, so no rewrite of the code holds it away. The file is the only one affected and the only one that gets
# the option.
set_source_files_properties(src/journal_seed.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU>:-Wno-maybe-uninitialized>")
# The pieces that differ by operating system (durable sync, the file lock, file protection): the files of
# src/platform/<os>/ are compiled and no others. Every directory defines the same functions from the same headers.
icl_platform_sources(ej_storage)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # file_protection.mm is Objective-C++ with reference counting, over Foundation's NSFileManager.
    set_source_files_properties(src/platform/ios/file_protection.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(ej_storage PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation>")
endif()
add_library(error_journaling::storage ALIAS ej_storage)

target_include_directories(ej_storage
    PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_compile_features(ej_storage PUBLIC cxx_std_23)
target_link_libraries(ej_storage
    PUBLIC ej_core
    PRIVATE Qt6::Sql icl::warnings icl::hardening icl::tracy icl::coverage)

if(ERROR_JOURNALING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
