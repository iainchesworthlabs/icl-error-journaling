# SPDX-License-Identifier: GPL-3.0-or-later
# ej_dataset: a desktop tool that writes the store of a facilitator who has used the app for a year (about 80,000
# fictional entries) through the real repository, for the cold-start measurement on a device (Stage 5, M1). It holds no
# harness code for the app: the owner puts the directory it writes on the phone with Xcode's Devices and Simulators
# window ("Replace Container", a development-signed build only). It borrows the sample builders of the core tests and the
# journal seeding of the storage library by include path, and changes neither.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Sql)

add_executable(ej_dataset ej_dataset.cpp)
target_link_libraries(ej_dataset PRIVATE ej_storage Qt6::Core Qt6::Sql icl::warnings icl::hardening)
target_include_directories(ej_dataset
    PRIVATE "${PROJECT_SOURCE_DIR}/libs/core/tests" "${PROJECT_SOURCE_DIR}/libs/storage/src")

# A small dataset through the tool. The tool refuses a directory that already holds a store, so a setup test clears it
# first and a run twice in one build tree passes both times.
set(ej_dataset_small_dir "${CMAKE_CURRENT_BINARY_DIR}/dataset-small")
add_test(NAME dataset.clean COMMAND ${CMAKE_COMMAND} -E rm -rf "${ej_dataset_small_dir}")
add_test(NAME dataset.small COMMAND ej_dataset --out "${ej_dataset_small_dir}" --logs 3 --entries 20)
set_tests_properties(dataset.clean PROPERTIES LABELS integration FIXTURES_SETUP dataset_small_dir)
set_tests_properties(dataset.small PROPERTIES LABELS integration TIMEOUT 120 FIXTURES_REQUIRED dataset_small_dir
    PASS_REGULAR_EXPRESSION "wrote 3 logs, 60 entries")
