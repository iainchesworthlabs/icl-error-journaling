# SPDX-License-Identifier: GPL-3.0-or-later
# What makes the application an Android program (docs/plan/stage-9.md, "Builds and toolchain"). Included by
# apps/app/CMakeLists.txt when ANDROID is set, which the toolchain file of Qt's Android kit does (the android-release and
# android-emulator presets).
#
# Qt's androiddeployqt and Gradle build the package from packaging/android/ (the manifest, the resources and the small
# Java layer), with the Qt libraries linked dynamically and copied into it. Unsigned by default, so the build needs no
# key; a signed bundle is the release workflow's, with the owner's upload key (Stage 9, package 9.9). Nothing here has run
# on a device: CI builds the package and looks inside it (tools/ci/check_android_package.py).

# Captured here: inside a function CMAKE_CURRENT_LIST_DIR is the caller's.
set(ERROR_JOURNALING_PACKAGING_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The versions of Android the package is for. The minimum is the oldest Qt 6.12 supports; the target is what Google Play
# requires of new apps and updates from 31 August 2026 (API 36, Android 16).
set(ERROR_JOURNALING_ANDROID_MIN_SDK 28)
set(ERROR_JOURNALING_ANDROID_TARGET_SDK 36)

# The version code (android:versionCode). Play refuses a code it has already seen for the application, so the release
# workflow gives the number of its run, as it does for the iOS build number. Empty: worked out from the project version.
set(ERROR_JOURNALING_ANDROID_VERSION_CODE "" CACHE STRING "Version code of the Android package (empty: from the project version)")

# The version code for a project version when none is given: MAJOR * 10000 + MINOR * 100 + PATCH, at least 1 (0.1.0 is
# 100). Minor and patch are below 100 in this project's versions; a larger one is refused rather than overlapping.
function(ej_android_version_code out version)
    if(NOT version MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
        message(FATAL_ERROR "ej_android_version_code: '${version}' is not MAJOR.MINOR.PATCH")
    endif()
    set(major "${CMAKE_MATCH_1}")
    set(minor "${CMAKE_MATCH_2}")
    set(patch "${CMAKE_MATCH_3}")
    if(minor GREATER 99 OR patch GREATER 99)
        message(FATAL_ERROR "ej_android_version_code: minor and patch of '${version}' must be below 100")
    endif()
    math(EXPR code "${major} * 10000 + ${minor} * 100 + ${patch}")
    if(code LESS 1)
        set(code 1)
    endif()
    set(${out} "${code}" PARENT_SCOPE)
endfunction()

# The version code the build uses: the given one, which must be a whole number from 1 to 2100000000 (Play's ceiling), or
# the one from the project version.
function(ej_android_resolved_version_code out given version)
    if(given STREQUAL "")
        ej_android_version_code(code "${version}")
    elseif(given MATCHES "^[1-9][0-9]*$" AND given LESS_EQUAL 2100000000)
        set(code "${given}")
    else()
        message(FATAL_ERROR "ERROR_JOURNALING_ANDROID_VERSION_CODE '${given}' is not a whole number from 1 to 2100000000")
    endif()
    set(${out} "${code}" PARENT_SCOPE)
endfunction()

# Makes the application target a program of this device.
function(ej_configure_android_app target)
    ej_android_resolved_version_code(version_code "${ERROR_JOURNALING_ANDROID_VERSION_CODE}" "${PROJECT_VERSION}")
    set_target_properties(${target} PROPERTIES
        QT_ANDROID_PACKAGE_SOURCE_DIR "${ERROR_JOURNALING_PACKAGING_DIR}/android"
        QT_ANDROID_PACKAGE_NAME "${ERROR_JOURNALING_APPLICATION_ID}"
        QT_ANDROID_APP_NAME "${ERROR_JOURNALING_DISPLAY_NAME}"
        QT_ANDROID_MIN_SDK_VERSION "${ERROR_JOURNALING_ANDROID_MIN_SDK}"
        QT_ANDROID_TARGET_SDK_VERSION "${ERROR_JOURNALING_ANDROID_TARGET_SDK}"
        QT_ANDROID_VERSION_NAME "${PROJECT_VERSION}"
        QT_ANDROID_VERSION_CODE "${version_code}")
    # 16 KB pages: Play requires it of targets of API 35 and up. NDK r27 links 4 KB-aligned by default (r28 does not);
    # the Qt kit is built for 16 KB, and these make the application's own library match. CI checks every library.
    target_link_options(${target} PRIVATE "-Wl,-z,max-page-size=16384" "-Wl,-z,common-page-size=16384")
    message(STATUS "Android package ${ERROR_JOURNALING_APPLICATION_ID} ${PROJECT_VERSION} (${version_code}), "
                   "API ${ERROR_JOURNALING_ANDROID_MIN_SDK} to ${ERROR_JOURNALING_ANDROID_TARGET_SDK}")
endfunction()
