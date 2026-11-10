# SPDX-License-Identifier: GPL-3.0-or-later
# The tests of the QML (docs/plan/stage-4.md, decision 1): Catch2 cases that load the QML offscreen and drive it from C++.
#
#   tests/ui/*_test.cpp      one file for each component and screen, named after it (`tile_test.cpp` for Tile), and the
#                            cases of the rig and of the theme; label `ui`, or `perf` for a case tagged [perf]
#   tests/ui/rig/            the rig: `UiRig`, the cross-cutting checks, the main
#   tests/ui/stubs/          QML used only by the tests of the rig (UiStub*.qml), compiled into the test module
#
# The sources are found by a glob, so a work package adds its test file and nothing else. The module ErrorJournalingApp is
# built a second time here from the same files as the application (an executable's resources cannot be linked into
# another executable), plus the stubs, which the application does not have.

find_package(Qt6 6.9 REQUIRED COMPONENTS Quick Test Gui)

include("${PROJECT_SOURCE_DIR}/apps/app/qml/qml_files.cmake")
ej_collect_qml_files(ej_ui_qml_files)

file(GLOB ej_ui_stub_files CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/stubs/*.qml")
foreach(file IN LISTS ej_ui_stub_files)
    get_filename_component(name "${file}" NAME)
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "qml/stubs/${name}")
endforeach()

file(GLOB ej_ui_rig_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/rig/*.cpp" "${CMAKE_CURRENT_SOURCE_DIR}/rig/*.hpp")
list(FILTER ej_ui_rig_sources EXCLUDE REGEX "/test_main\.cpp$")
file(GLOB ej_ui_test_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*_test.cpp")

add_library(ej_ui_test_support STATIC ${ej_ui_rig_sources})
# Timings in a build with sanitizers, coverage or no optimisation are several times slower; the perf cases scale their
# budget by this, unless EJ_PERF_BUDGET_FACTOR is set in the environment.
set(ej_perf_budget_factor 1)
if(ICL_SANITIZERS OR ICL_ENABLE_COVERAGE OR CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(ej_perf_budget_factor 10)
endif()
target_compile_definitions(ej_ui_test_support PRIVATE EJ_PERF_BUDGET_FACTOR_DEFAULT=${ej_perf_budget_factor})
target_include_directories(ej_ui_test_support PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
# The rig advances the TestClock and the ManualStorageGateway of Stage 3; "support/<name>.hpp" is theirs, "rig/<name>.hpp" ours.
target_link_libraries(ej_ui_test_support
    PUBLIC ej_viewmodel_test_support Qt6::Quick Qt6::Gui Qt6::Test Catch2::Catch2
    PRIVATE icl::warnings icl::coverage)

qt_add_executable(ej_ui_tests ${ej_ui_test_sources} "${CMAKE_CURRENT_SOURCE_DIR}/rig/test_main.cpp")
qt_add_qml_module(ej_ui_tests
    URI ErrorJournalingApp
    VERSION 1.0
    QML_FILES ${ej_ui_qml_files} ${ej_ui_stub_files})
target_compile_definitions(ej_ui_tests PRIVATE
    EJ_TOKENS_JSON="${PROJECT_SOURCE_DIR}/docs/design/tokens.json"
    EJ_PERF_BUDGET="${CMAKE_CURRENT_SOURCE_DIR}/perf-budget.json"
    EJ_SAMPLE_CONFIG="${PROJECT_SOURCE_DIR}/config/sample-config.json"
    EJ_SCREENS_JSON="${PROJECT_SOURCE_DIR}/tools/ej_screenshots/screens.json"
    EJ_QML_SCREENS_DIR="${PROJECT_SOURCE_DIR}/apps/app/qml/screens")
target_link_libraries(ej_ui_tests PRIVATE ej_ui_test_support Qt6::Quick Qt6::Test Catch2::Catch2 icl::warnings icl::coverage)
icl_relax_generated_warnings(ej_ui_tests)

# The test main sets the platform and the renderer itself, so the one thing the environment has to carry is the TSan
# suppressions. (catch_discover_tests takes one ENVIRONMENT value; a list of several would be read as other properties.)
set(ej_ui_test_properties "")
if(ICL_SANITIZERS MATCHES "thread")
    set(ej_ui_test_properties ENVIRONMENT "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan.supp")
endif()
# Cases tagged [perf] have the label perf and run in the last phase; the others are ui. screenshots.render renders every
# screen in every mode (tools/ej_screenshots), which takes longer than one case is given, so it has a timeout of its own.
ej_discover_tests(ej_ui_tests
    TEST_SPEC "~[perf]~[screenshots]"
    PROPERTIES LABELS ui TIMEOUT 120 ${ej_ui_test_properties})
ej_discover_tests(ej_ui_tests
    TEST_SPEC "[screenshots]"
    TEST_SUFFIX " (screenshots)"
    PROPERTIES LABELS ui TIMEOUT 900 ${ej_ui_test_properties})
ej_discover_tests(ej_ui_tests
    TEST_SPEC "[perf]"
    TEST_SUFFIX " (perf)"
    PROPERTIES LABELS perf TIMEOUT 120 ${ej_ui_test_properties})

# ---- the checks that keep the QML honest (tools/ci/check_qml_*.py, docs/ci.md) ----

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    set(ej_qml_root "${PROJECT_SOURCE_DIR}/apps/app/qml")
    set(ej_check_dir "${PROJECT_SOURCE_DIR}/tools/ci")
    function(ej_add_qml_check name script)
        add_test(NAME ${name} COMMAND "${Python3_EXECUTABLE}" "${ej_check_dir}/${script}" --root "${PROJECT_SOURCE_DIR}" ${ARGN})
        set_tests_properties(${name} PROPERTIES LABELS unit TIMEOUT 60)
    endfunction()
    ej_add_qml_check(qml.platform check_qml_platform.py)
    ej_add_qml_check(theme.no_literals check_qml_literals.py)
    ej_add_qml_check(i18n.strings check_qml_strings.py)
    ej_add_qml_check(qml.tests check_qml_tests.py)
    ej_add_qml_check(motion.animators check_qml_motion.py)

    # qmlformat has no verify option: the script formats each file to text and compares. Qt ships the program next to
    # the other tools of the kit; a kit without it skips the check, and says so.
    find_program(ej_qmlformat qmlformat HINTS "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}" "${Qt6_DIR}/../../../bin" "${Qt6_DIR}/../../../libexec")
    if(ej_qmlformat)
        ej_add_qml_check(qml.format check_qml_format.py --qmlformat "${ej_qmlformat}")
    else()
        message(STATUS "qmlformat not found: the test qml.format is not added")
    endif()
else()
    message(STATUS "Python 3 not found: the QML policy checks are not added as tests (CI runs them)")
endif()

# qmllint, with every warning an error (apps/app/qml/.qmllint.ini), over the test module, which holds all the QML and the
# stubs. The target is the one qt_add_qml_module made; it builds with the module, so it needs a build, not a program run.
if(TARGET ej_ui_tests_qmllint)
    add_test(NAME qml.lint COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --target ej_ui_tests_qmllint)
    set_tests_properties(qml.lint PROPERTIES LABELS unit TIMEOUT 300)
endif()
