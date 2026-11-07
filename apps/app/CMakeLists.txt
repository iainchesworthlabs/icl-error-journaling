# SPDX-License-Identifier: GPL-3.0-or-later
# The application: an empty Qt Quick window. On iOS (the product) Xcode builds the bundle; on a Mac it is a desktop
# program for iterating on the user interface; on Linux it is built and tested in CI, and installed nowhere.

# Sql: ej_storage links Qt Sql privately and a static library's dependencies have to be declared where it is linked.
find_package(Qt6 6.9 REQUIRED COMPONENTS Core Quick Sql)
qt_standard_project_setup(REQUIRES 6.9)

# The view-model layer (Stage 3, docs/plan/stage-3.md): the thin QObject layer between the core and storage on one side
# and QML on the other. A static library, so that ej_app links it and the tests can too (an executable cannot be linked).
# It links no Qt Quick and no QtGui, so its tests run without a display. The sources are found by a glob, as in
# libs/ios_bridge, so that a work package adds its files without editing this file: a new file in src/viewmodel is
# compiled and, when it has Q_OBJECT, moc'd, with no change here (CONFIGURE_DEPENDS re-runs the glob at build time).
file(GLOB ej_viewmodel_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/viewmodel/*.hpp")
add_library(ej_viewmodel STATIC ${ej_viewmodel_sources})
add_library(error_journaling::viewmodel ALIAS ej_viewmodel)
# Headers are included as "viewmodel/<name>.hpp".
target_include_directories(ej_viewmodel PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_compile_features(ej_viewmodel PUBLIC cxx_std_23)
target_link_libraries(ej_viewmodel
    PUBLIC ej_core ej_storage ej_ios_bridge Qt6::Core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)
icl_relax_generated_warnings(ej_viewmodel)

# The composition root (Stage 3, 3.11): AppRoot builds the services and wires them, and start_sqlite_storage opens the
# real store. QtCore only, so the headless tests build the app the way main.cpp does (tests/viewmodel/support/rig.hpp).
add_library(ej_app_root STATIC src/app_root.cpp src/app_root.hpp)
add_library(error_journaling::app_root ALIAS ej_app_root)
target_include_directories(ej_app_root PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>")
target_compile_features(ej_app_root PUBLIC cxx_std_23)
target_link_libraries(ej_app_root
    PUBLIC ej_viewmodel ej_core ej_storage ej_ios_bridge Qt6::Core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)
icl_relax_generated_warnings(ej_app_root)

# qt_clipboard needs QtGui (Qt6::Quick brings it), which ej_viewmodel and ej_app_root do not link.
qt_add_executable(ej_app src/main.cpp src/qt_clipboard.cpp src/qt_clipboard.hpp)
# The executable is named error-journaling while the target is ej_app.
set_target_properties(ej_app PROPERTIES OUTPUT_NAME error-journaling)

# QML is compiled ahead of time by qmlcachegen. The URI must differ from the executable name: the module gets a
# directory of that name in the build tree, and on a case-insensitive file system (macOS, Windows) two names that differ
# only in case are the same path.
include("${CMAKE_CURRENT_SOURCE_DIR}/qml/qml_files.cmake")
ej_collect_qml_files(ej_qml_files)
qt_add_qml_module(ej_app
    URI ErrorJournalingApp
    VERSION 1.0
    QML_FILES ${ej_qml_files})

target_link_libraries(ej_app
    PRIVATE ej_app_root ej_viewmodel ej_core ej_storage ej_ios_bridge ej_qml_bridge Qt6::Quick icl::warnings icl::hardening icl::coverage)
# qmlcachegen, rcc and moc write C++ files that the strict warning set rejects with some compilers.
icl_relax_generated_warnings(ej_app)

if(IOS)
    include("${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios.cmake")
    ej_configure_ios_app(ej_app)
else()
    # Installs the program and copies the Qt libraries, plugins and QML modules next to it (macOS: a bundle). Desktop
    # builds are for development and CI; nothing is released from them (docs/releasing.md).
    icl_install_qt_app(ej_app
        BUNDLE_IDENTIFIER ${ERROR_JOURNALING_APPLICATION_ID}
        DISPLAY_NAME "${ERROR_JOURNALING_DISPLAY_NAME}"
        NOTICE_EXCLUDE catch2)
endif()

if(ERROR_JOURNALING_BUILD_TESTS)
    # The tests of the view-model layer (unit, integration, perf), in tests/viewmodel.
    add_subdirectory(tests/viewmodel)

    set(ej_app_test_environment QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QSG_RENDER_LOOP=basic)
    if(ICL_SANITIZERS MATCHES "thread")
        # The prebuilt Qt libraries are not instrumented; the file says why.
        list(APPEND ej_app_test_environment
            "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan.supp")
    endif()

    # The tests of the QML (Stage 4, docs/plan/stage-4.md), in tests/ui.
    add_subdirectory(tests/ui)

    # Starts the real application without a display, waits for the first frame and exits.
    # It opens a real store, so it is given a folder of its own and leaves the person's data alone.
    add_test(NAME app.smoke COMMAND ej_app --smoke-test --data-dir "${CMAKE_CURRENT_BINARY_DIR}/smoke-data")
    set_tests_properties(app.smoke PROPERTIES LABELS ui TIMEOUT 60 ENVIRONMENT "${ej_app_test_environment}")

    # The signing identity is the distribution certificate unless the build is told otherwise (a script test: nothing
    # here configures for iOS).
    add_test(NAME ios.signing_identity
        COMMAND ${CMAKE_COMMAND} -DIOS_CMAKE=${CMAKE_CURRENT_SOURCE_DIR}/packaging/ios.cmake
                -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/check_signing_identity.cmake)
    set_tests_properties(ios.signing_identity PROPERTIES LABELS unit TIMEOUT 30)

    # The version and the help are answered without a window system: the platform plugin is one that does not exist.
    add_test(NAME app.version COMMAND ej_app --version)
    set_tests_properties(app.version PROPERTIES
        LABELS ui
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=there-is-no-such-plugin"
        PASS_REGULAR_EXPRESSION "^error-journaling [0-9]+\\.[0-9]+\\.[0-9]+")
endif()
