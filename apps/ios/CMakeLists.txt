# SPDX-License-Identifier: GPL-3.0-or-later
# The iPhone and iPad application. Included by the top-level CMakeLists.txt when the target is iOS (the `ios-release`
# preset). Xcode builds a flat bundle from packaging/Info.plist.in and the asset catalogue.
#
# Unsigned by default: the build needs no Apple account. A signed build is the release workflow's, with the owner's
# certificate and provisioning profile (docs/releasing.md, "iOS and TestFlight").

# The build number (CFBundleVersion). TestFlight refuses a build whose number is not higher than every earlier one for the
# same version, so the release workflow gives the number of its run. Empty: the project version.
set(ICL_TEMPLATE_IOS_BUILD_NUMBER "" CACHE STRING "Build number of the iOS application (empty: the project version)")
# The team and the provisioning profile the application is signed with. Both are empty for the unsigned build, and the
# release workflow gives both from the profile it checked.
set(ICL_TEMPLATE_IOS_SIGNING_TEAM "" CACHE STRING "Apple team ID the iOS application is signed for (empty: an unsigned build)")
set(ICL_TEMPLATE_IOS_SIGNING_PROFILE "" CACHE STRING "Name of the provisioning profile the iOS application is signed with (empty: an unsigned build)")

if(ICL_TEMPLATE_IOS_BUILD_NUMBER)
    set(build_number "${ICL_TEMPLATE_IOS_BUILD_NUMBER}")
else()
    set(build_number "${PROJECT_VERSION}")
endif()

set(assets "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Assets.xcassets")

add_executable(icl_template_ios MACOSX_BUNDLE src/main.mm "${assets}")
set_target_properties(icl_template_ios PROPERTIES OUTPUT_NAME icl-template)
# Objective-C++ with reference counting, like the platform sources of the libraries.
set_source_files_properties(src/main.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
target_link_libraries(icl_template_ios
    PRIVATE icl_template::core icl::warnings icl::hardening "-framework UIKit" "-framework Foundation")

set_target_properties(icl_template_ios PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "${ICL_TEMPLATE_DISPLAY_NAME}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${ICL_TEMPLATE_APPLICATION_ID}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${build_number}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (C) Iain Chesworth Labs contributors"
    # iPhone and iPad.
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    # The icon set Info.plist.in's CFBundleIconName names; without this Xcode has nothing to compile that slot from and
    # the home screen shows a blank icon.
    XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
# A source path ending in .xcassets is what tells the Xcode generator to compile the catalogue, not copy it.
set_source_files_properties("${assets}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

# Signing, for a build that is signed. They sign the application target alone: signing every Xcode target forces Xcode to
# sign static libraries, which have no bundle identifier and were never meant to be signed.
if(ICL_TEMPLATE_IOS_SIGNING_TEAM AND ICL_TEMPLATE_IOS_SIGNING_PROFILE)
    set_target_properties(icl_template_ios PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
        XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
        XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Distribution"
        XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${ICL_TEMPLATE_IOS_SIGNING_TEAM}"
        XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${ICL_TEMPLATE_IOS_SIGNING_PROFILE}")
elseif(ICL_TEMPLATE_IOS_SIGNING_TEAM OR ICL_TEMPLATE_IOS_SIGNING_PROFILE)
    message(FATAL_ERROR "ICL_TEMPLATE_IOS_SIGNING_TEAM and ICL_TEMPLATE_IOS_SIGNING_PROFILE go together: one of them is set "
        "and the other is empty. Give both for a signed build, or neither for an unsigned one (docs/building.md, iOS).")
endif()
