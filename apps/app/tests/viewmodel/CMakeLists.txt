# SPDX-License-Identifier: GPL-3.0-or-later
# The tests of ej_viewmodel (docs/plan/stage-3.md). Like the sources, they are found by a glob, so that a work package adds
# its tests without editing this file:
#
#   tests/viewmodel/test_*.cpp          `unit`: the in-memory repository, the fakes and ManualStorageGateway; no thread
#   tests/viewmodel/integration/*.cpp   `integration`: ThreadedStorageGateway, the real store, the threading tests
#   tests/viewmodel/perf/*.cpp          `perf`: the budgets of the threading section (built only when the directory has tests)
#   tests/viewmodel/support/            the test support (TestClock, EventLog, RecordingRepository, the fakes), linked into all of them
#
# The cases are Catch2 and link Qt6::Test for QSignalSpy only. Each executable owns a QCoreApplication (support/test_main.cpp).

find_package(Qt6 6.9 REQUIRED COMPONENTS Test)

file(GLOB ej_viewmodel_support_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/support/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/support/*.hpp")
# The main is linked into each executable, not into the library.
list(FILTER ej_viewmodel_support_sources EXCLUDE REGEX "/test_main\\.cpp$")

add_library(ej_viewmodel_test_support STATIC ${ej_viewmodel_support_sources})
# Headers are included as "support/<name>.hpp".
target_include_directories(ej_viewmodel_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(ej_viewmodel_test_support
    PRIVATE EJ_SAMPLE_CONFIG="${PROJECT_SOURCE_DIR}/config/sample-config.json")
# ej_core_test_support brings the golden comparison, the scenarios and the fixed-offset zone provider of the core tests.
target_link_libraries(ej_viewmodel_test_support
    PUBLIC ej_viewmodel ej_core_test_support Qt6::Core Catch2::Catch2
    PRIVATE icl::warnings icl::coverage)

set(ej_viewmodel_test_properties "")
if(ICL_SANITIZERS MATCHES "thread")
    # The prebuilt Qt libraries are not instrumented; tools/sanitizers/tsan.supp says why.
    set(ej_viewmodel_test_properties
        ENVIRONMENT "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan.supp")
endif()

# Adds one executable from the sources in `${ARGN}`, with the main that owns the QCoreApplication.
function(ej_add_viewmodel_tests target label)
    add_executable(${target} ${ARGN} "${CMAKE_CURRENT_SOURCE_DIR}/support/test_main.cpp")
    target_link_libraries(${target}
        PRIVATE ej_viewmodel_test_support Qt6::Test Catch2::Catch2 icl::warnings icl::coverage)
    catch_discover_tests(${target} PROPERTIES LABELS ${label} ${ej_viewmodel_test_properties})
endfunction()

file(GLOB ej_viewmodel_unit_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp")
ej_add_viewmodel_tests(ej_viewmodel_tests unit ${ej_viewmodel_unit_sources})

file(GLOB ej_viewmodel_integration_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/integration/*.cpp")
ej_add_viewmodel_tests(ej_viewmodel_integration_tests integration ${ej_viewmodel_integration_sources})

file(GLOB ej_viewmodel_perf_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/perf/*.cpp")
if(ej_viewmodel_perf_sources)
    ej_add_viewmodel_tests(ej_viewmodel_perf_tests perf ${ej_viewmodel_perf_sources})
endif()
