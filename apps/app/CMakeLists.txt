# SPDX-License-Identifier: GPL-3.0-or-later
# The application: an empty Qt Quick window. On iOS (the product) Xcode builds the bundle; on a Mac it is a desktop
# program for iterating on the user interface; on Linux it is built and tested in CI, and installed nowhere.

find_package(Qt6 6.9 REQUIRED COMPONENTS Quick)
qt_standard_project_setup(REQUIRES 6.9)

qt_add_executable(ej_app src/main.cpp)
# The executable is named error-journaling while the target is ej_app.
set_target_properties(ej_app PROPERTIES OUTPUT_NAME error-journaling)

# QML is compiled ahead of time by qmlcachegen. The URI must differ from the executable name: the module gets a
# directory of that name in the build tree, and on a case-insensitive file system (macOS, Windows) two names that differ
# only in case are the same path.
qt_add_qml_module(ej_app
    URI ErrorJournalingApp
    VERSION 1.0
    QML_FILES qml/Main.qml)

target_link_libraries(ej_app
    PRIVATE ej_core ej_storage ej_ios_bridge ej_qml_bridge Qt6::Quick icl::warnings icl::hardening icl::coverage)
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
    set(ej_app_test_environment QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software QSG_RENDER_LOOP=basic)
    if(ICL_SANITIZERS MATCHES "thread")
        # The prebuilt Qt libraries are not instrumented; the file says why.
        list(APPEND ej_app_test_environment
            "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan.supp")
    endif()

    # Starts the real application without a display, waits for the first frame and exits.
    add_test(NAME app.smoke COMMAND ej_app --smoke-test)
    set_tests_properties(app.smoke PROPERTIES LABELS ui TIMEOUT 60 ENVIRONMENT "${ej_app_test_environment}")

    # The version and the help are answered without a window system: the platform plugin is one that does not exist.
    add_test(NAME app.version COMMAND ej_app --version)
    set_tests_properties(app.version PROPERTIES
        LABELS ui
        TIMEOUT 30
        ENVIRONMENT "QT_QPA_PLATFORM=there-is-no-such-plugin"
        PASS_REGULAR_EXPRESSION "^error-journaling [0-9]+\\.[0-9]+\\.[0-9]+")
endif()
