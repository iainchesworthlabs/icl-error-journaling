# SPDX-License-Identifier: GPL-3.0-or-later
# ej_launch_image: a desktop tool that renders the skeleton of the Log screen (qml/screens/launch/LaunchSkeleton.qml) into the
# launch image of the iOS asset catalogue, in light and dark (Stage 5, package 5.8), and checks that the PNGs in the catalogue
# are what the skeleton draws now. It loads the QML module the way the application and the UI tests do, from the same files:
# an executable's resources cannot be linked into another executable, so it builds the module a third time.
find_package(Qt6 6.9 REQUIRED COMPONENTS Quick Gui)
# AUTOMOC and the QML module policies, as apps/app has them (a directory sets them for itself and below).
qt_standard_project_setup(REQUIRES 6.9)

include("${PROJECT_SOURCE_DIR}/apps/app/qml/qml_files.cmake")
ej_collect_qml_files(ej_launch_qml_files)

qt_add_executable(ej_launch_image ej_launch_image.cpp)
qt_add_qml_module(ej_launch_image
    URI ErrorJournalingApp
    VERSION 1.0
    QML_FILES ${ej_launch_qml_files})
target_compile_definitions(ej_launch_image PRIVATE
    EJ_LAUNCH_IMAGE_DIR="${PROJECT_SOURCE_DIR}/apps/app/packaging/ios/Assets.xcassets/LaunchBoard.imageset")
target_link_libraries(ej_launch_image PRIVATE Qt6::Quick Qt6::Gui icl::warnings icl::hardening)
icl_relax_generated_warnings(ej_launch_image)

# The tests of the launch image (label ui): the PNGs in the asset catalogue are what the skeleton draws now, light and dark
# (docs/plan/stage-5.md, launch.image_current), and the comparison can fail (launch.image_compare). On Windows the programs
# need the Qt DLLs on PATH (cmake/EjQtTestEnvironment.cmake).
add_test(NAME launch.image_current COMMAND ej_launch_image --check)
add_test(NAME launch.image_compare COMMAND ej_launch_image --self-test)
# The prebuilt Qt libraries are not instrumented for ThreadSanitizer; the UI tests use the same suppressions.
set(ej_launch_test_properties "")
if(ICL_SANITIZERS MATCHES "thread")
    set(ej_launch_test_properties ENVIRONMENT "TSAN_OPTIONS=suppressions=${PROJECT_SOURCE_DIR}/tools/sanitizers/tsan.supp")
endif()
set_tests_properties(launch.image_current launch.image_compare PROPERTIES LABELS ui TIMEOUT 120 ${ej_launch_test_properties})
ej_qt_test_environment(launch.image_current launch.image_compare)
