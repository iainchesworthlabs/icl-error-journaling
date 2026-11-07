# SPDX-License-Identifier: GPL-3.0-or-later

# `system`: the kill harness (section 4 of docs/plan/stage-2.md). ej_kill_worker is the child the driver starts and kills;
# ej_kill_driver is the Catch2 test. CTest runs the driver with the PR-gate counts (25 random kills); the merge queue, the
# nightly run and the exit gate run the same driver with more (docs/ci.md).
find_package(Qt6 6.8 REQUIRED COMPONENTS Sql)

# The workload both processes build from the same seed.
add_library(ej_kill_workload STATIC kill_workload.cpp)
target_link_libraries(ej_kill_workload PUBLIC ej_storage PRIVATE icl::warnings icl::coverage)
target_include_directories(ej_kill_workload
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/libs/core/tests")

add_executable(ej_kill_worker kill_worker.cpp)
target_link_libraries(ej_kill_worker PRIVATE ej_kill_workload Qt6::Sql icl::warnings icl::coverage)
# migration_steps.hpp, the test-only migration steps, lives in the parent directory.
target_include_directories(ej_kill_worker PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(ej_kill_driver kill_driver.cpp)
target_link_libraries(ej_kill_driver PRIVATE ej_kill_workload Qt6::Sql Catch2::Catch2 icl::warnings icl::coverage)
# recovery_fixture.hpp and the helpers it includes live in the parent directory.
target_include_directories(ej_kill_driver PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.." "${PROJECT_SOURCE_DIR}/libs/storage/src")
# platform/<os>/process_containment.cpp: ends the workers with the driver (a Job Object on Windows).
icl_platform_sources(ej_kill_driver DIR "${CMAKE_CURRENT_SOURCE_DIR}/platform")
# The driver starts the worker by file name, so the worker is built whenever the driver is.
add_dependencies(ej_kill_driver ej_kill_worker)

catch_discover_tests(ej_kill_driver PROPERTIES LABELS system TIMEOUT 900)
