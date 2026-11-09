# SPDX-License-Identifier: GPL-3.0-or-later
# The facts of the About page that the build knows (docs/prd.md PRF-12; docs/licensing.md).
#
#   ej_add_about_data(<app target>)
#       Adds two targets that the application depends on, and the tests that keep them honest.
#
#       ej_build_info   runs tools/licences/build_info.py on every build and writes build/about/build_info_values.cpp:
#                       the short commit hash, the tag or branch, the build number, and the date of the commit (or
#                       $SOURCE_DATE_EPOCH). A tree without git, or a machine without it, says "unknown" and builds.
#                       The file changes only when a value does, so an unchanged commit recompiles nothing.
#       ej_notices      runs tools/licences/notices.py before the application is linked. It reads the application's
#                       resolved link line from the CMake file API, finds every third-party component on it (Qt and what
#                       Qt bundles, vcpkg ports) and writes third-party-notices.json and THIRD-PARTY-NOTICES.txt. The JSON
#                       is embedded in the application as the resource :/about/third-party-notices.json. A component
#                       with no licence entry stops the build (ERROR_JOURNALING_NOTICES_STRICT).
#
#   Options:
#       ERROR_JOURNALING_NOTICES_STRICT   ON (the default): a link input that the notices cannot account for fails the
#                                         build. OFF only to get past a problem while it is being fixed.
#
#   Cache variables read: ERROR_JOURNALING_IOS_BUILD_NUMBER (the iOS CFBundleVersion; empty means the project version).
include_guard(GLOBAL)

option(ERROR_JOURNALING_NOTICES_STRICT "Fail the build when a linked third-party component has no licence entry" ON)

# The file API reply that notices.py reads is written at the end of every configure, so the query has to exist before the
# first one ends.
cmake_file_api(QUERY API_VERSION 1 CODEMODEL 2)

function(ej_add_about_data target)
    # find_program and not find_package(Python3): an iOS toolchain restricts the search to the target's root path, and the
    # script runs on the build machine.
    find_program(ej_python NAMES python3 python NO_CMAKE_FIND_ROOT_PATH REQUIRED
        DOC "Python 3, which runs tools/licences (build information and third-party notices)")
    set(tools "${PROJECT_SOURCE_DIR}/tools/licences")
    set(about_dir "${CMAKE_CURRENT_BINARY_DIR}/about")
    file(MAKE_DIRECTORY "${about_dir}")

    # ---- the commit, the build number and the date ----
    if(IOS)
        set(build_number "${ERROR_JOURNALING_IOS_BUILD_NUMBER}")
        if(NOT build_number)
            set(build_number "${PROJECT_VERSION}")
        endif()
    else()
        set(build_number "")
    endif()
    set(values "${about_dir}/build_info_values.cpp")
    set(build_info_command "${ej_python}" -I "${tools}/build_info.py"
        --source-dir "${PROJECT_SOURCE_DIR}" --out "${values}" "--build-number=${build_number}"
        "--source-url=${PROJECT_HOMEPAGE_URL}")
    # Once now, so that the file exists for the generator and the IDE; then at every build.
    execute_process(COMMAND ${build_info_command} RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "tools/licences/build_info.py failed (${status})")
    endif()
    add_custom_target(ej_build_info
        COMMAND ${build_info_command}
        BYPRODUCTS "${values}"
        COMMENT "Recording the commit and the build date for the About page"
        VERBATIM)
    set_source_files_properties("${values}" PROPERTIES GENERATED TRUE)
    target_sources(${target} PRIVATE "${values}")
    add_dependencies(${target} ej_build_info)

    # ---- the third-party notices ----
    if(IOS)
        set(platform "ios")
    elseif(ANDROID)
        set(platform "android")
    elseif(APPLE)
        set(platform "macos")
    elseif(WIN32)
        set(platform "windows")
    else()
        set(platform "linux")
    endif()
    set(qt_prefix "")
    if(DEFINED Qt6_DIR)
        get_filename_component(qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
    endif()
    set(triplet_dir "")
    if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
        set(triplet_dir "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
    endif()
    set(notices_arguments
        --binary-dir "${CMAKE_BINARY_DIR}" --source-dir "${PROJECT_SOURCE_DIR}" --target ${target}
        --platform ${platform} "--config=$<CONFIG>" "--qt-prefix=${qt_prefix}" "--qt-version=${Qt6_VERSION}"
        "--vcpkg-triplet-dir=${triplet_dir}" --out-dir "${about_dir}")
    set(notices_json "${about_dir}/third-party-notices.json")
    set(notices_text "${about_dir}/THIRD-PARTY-NOTICES.txt")
    # Placeholders, so that the resource below names files that exist at configure time; the target replaces them with the
    # real ones before anything is linked.
    if(NOT EXISTS "${notices_json}")
        file(WRITE "${notices_json}" "{\"schema\":1,\"components\":[],\"texts\":{},\"unresolved\":[\"not generated yet\"]}\n")
    endif()
    if(NOT EXISTS "${notices_text}")
        file(WRITE "${notices_text}" "Third-party notices have not been generated yet.\n")
    endif()
    set(strict "")
    if(ERROR_JOURNALING_NOTICES_STRICT)
        set(strict --strict)
    endif()
    add_custom_target(ej_notices
        COMMAND "${ej_python}" -I "${tools}/notices.py" ${notices_arguments} ${strict}
        BYPRODUCTS "${notices_json}" "${notices_text}"
        COMMENT "Collecting the licences of what ${target} links"
        VERBATIM)
    add_dependencies(${target} ej_notices)
    qt_add_resources(${target} ej_about_notices
        PREFIX "/about"
        BASE "${about_dir}"
        FILES "${notices_json}")

    # ---- tests ----
    if(ERROR_JOURNALING_BUILD_TESTS)
        # The same generator over the same link line, strict and writing nothing: a component without a licence entry fails
        # here as it fails the build, so a desktop run of ctest catches what an iPhone build would.
        add_test(NAME about.notices
            COMMAND "${ej_python}" -I "${tools}/notices.py" ${notices_arguments} --strict --check)
        set_tests_properties(about.notices PROPERTIES LABELS unit TIMEOUT 120
            FAIL_REGULAR_EXPRESSION "UNRESOLVED")
        add_test(NAME about.build_info
            COMMAND "${ej_python}" -I "${tools}/build_info.py" --source-dir "${PROJECT_SOURCE_DIR}" --print
                "--build-number=${build_number}")
        set_tests_properties(about.build_info PROPERTIES LABELS unit TIMEOUT 60
            PASS_REGULAR_EXPRESSION "\"commit\": \"([0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f]|unknown)\"")
        # The unit tests of the two scripts, over made-up link lines and a made-up repository.
        add_test(NAME about.tools
            COMMAND "${ej_python}" -I -m unittest discover -s "${PROJECT_SOURCE_DIR}/tools/tests" -p test_notices.py)
        set_tests_properties(about.tools PROPERTIES LABELS unit TIMEOUT 180)
        # Qt's own SBOMs, when the kit has them, against the snapshot: a Qt update that changes a licence shows here.
        if(platform MATCHES "^(windows|macos)$" AND EXISTS "${qt_prefix}/sbom")
            add_test(NAME about.qt_snapshot
                COMMAND "${ej_python}" -I "${tools}/qt_sbom_manifest.py" --check
                    --sbom "${platform}=${qt_prefix}/sbom" --out "${tools}/data/qt-modules.json")
            set_tests_properties(about.qt_snapshot PROPERTIES LABELS unit TIMEOUT 180)
        endif()
    endif()
endfunction()
