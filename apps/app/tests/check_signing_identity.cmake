# SPDX-License-Identifier: GPL-3.0-or-later
# Run with: cmake -DIOS_CMAKE=<path to packaging/ios.cmake> -P check_signing_identity.cmake
# The signing identity is "Apple Distribution" unless ERROR_JOURNALING_IOS_SIGNING_IDENTITY is given, and the file uses the
# variable, not the literal.
if(NOT IOS_CMAKE)
    message(FATAL_ERROR "IOS_CMAKE is not set")
endif()

include("${IOS_CMAKE}")
if(NOT ERROR_JOURNALING_IOS_SIGNING_IDENTITY STREQUAL "Apple Distribution")
    message(FATAL_ERROR "the default signing identity is '${ERROR_JOURNALING_IOS_SIGNING_IDENTITY}', not 'Apple Distribution'")
endif()

file(READ "${IOS_CMAKE}" ios_cmake_text)
string(FIND "${ios_cmake_text}" "XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY \"\${ERROR_JOURNALING_IOS_SIGNING_IDENTITY}\"" found_at)
if(found_at EQUAL -1)
    message(FATAL_ERROR "ios.cmake does not sign with ERROR_JOURNALING_IOS_SIGNING_IDENTITY")
endif()
