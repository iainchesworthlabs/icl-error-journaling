# SPDX-License-Identifier: GPL-3.0-or-later
# Run with: cmake -DANDROID_CMAKE=<path to packaging/android.cmake> [-DGIVEN=<code>] -P check_android_version_code.cmake
# Without GIVEN: the version code worked out from a project version, and a given code that is passed through. With
# GIVEN: resolves that code, which the test that passes it expects to fail (an empty, non-numeric or too large code).
if(NOT ANDROID_CMAKE)
    message(FATAL_ERROR "ANDROID_CMAKE is not set")
endif()

include("${ANDROID_CMAKE}")

if(DEFINED GIVEN)
    ej_android_resolved_version_code(code "${GIVEN}" "0.1.0")
    message(STATUS "version code ${code}")
    return()
endif()

function(expect version expected)
    ej_android_version_code(code "${version}")
    if(NOT code STREQUAL expected)
        message(FATAL_ERROR "the version code of ${version} is ${code}, expected ${expected}")
    endif()
endfunction()

expect("0.0.0" 1)
expect("0.1.0" 100)
expect("1.0.0" 10000)
expect("1.2.3" 10203)
expect("2.99.99" 29999)

# Codes rise with the version.
ej_android_version_code(lower "1.9.9")
ej_android_version_code(higher "1.10.0")
if(NOT higher GREATER lower)
    message(FATAL_ERROR "1.10.0 (${higher}) is not above 1.9.9 (${lower})")
endif()

# A given code is used as it is; an empty one falls back to the project version.
ej_android_resolved_version_code(code "4711" "0.1.0")
if(NOT code STREQUAL "4711")
    message(FATAL_ERROR "the given code 4711 became ${code}")
endif()
ej_android_resolved_version_code(code "" "0.1.0")
if(NOT code STREQUAL "100")
    message(FATAL_ERROR "an empty code for 0.1.0 became ${code}, expected 100")
endif()

if(NOT ERROR_JOURNALING_ANDROID_MIN_SDK EQUAL 28 OR NOT ERROR_JOURNALING_ANDROID_TARGET_SDK EQUAL 36)
    message(FATAL_ERROR "the SDK range is ${ERROR_JOURNALING_ANDROID_MIN_SDK} to ${ERROR_JOURNALING_ANDROID_TARGET_SDK}, expected 28 to 36")
endif()
