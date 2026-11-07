# SPDX-License-Identifier: GPL-3.0-or-later
# What makes the application an iPhone program. Included by apps/app/CMakeLists.txt when IOS is set, by the toolchain
# file of Qt's iOS kit (the ios-release and ios-simulator presets).
#
# No `cmake --install`, no CPack and no deploy script: Xcode builds a bundle with Qt linked in statically, from
# packaging/ios/Info.plist.in and the asset catalogue. Unsigned by default, so the build needs no Apple account; a signed
# build is the release workflow's, with the owner's certificate and provisioning profile (docs/releasing.md, "iOS and
# TestFlight"). Nothing here has run on a device: CI builds the bundle and looks inside it (docs/ci.md).

# Captured here: inside a function CMAKE_CURRENT_LIST_DIR is the caller's.
set(ERROR_JOURNALING_PACKAGING_DIR "${CMAKE_CURRENT_LIST_DIR}")

# The build number (CFBundleVersion). TestFlight refuses a build whose number is not higher than every earlier one for the
# same version, so the release workflow gives the number of its run. Empty: the project version.
set(ERROR_JOURNALING_IOS_BUILD_NUMBER "" CACHE STRING "Build number of the iOS application (empty: the project version)")
# The team and the provisioning profile the application is signed with. Both are empty for the unsigned build, and the
# release workflow gives both from the profile it checked.
set(ERROR_JOURNALING_IOS_SIGNING_TEAM "" CACHE STRING "Apple team ID the iOS application is signed for (empty: an unsigned build)")
set(ERROR_JOURNALING_IOS_SIGNING_PROFILE "" CACHE STRING "Name of the provisioning profile the iOS application is signed with (empty: an unsigned build)")

# The identity the application is signed with. The release workflow signs with the distribution certificate, which is the
# default. Instruments cannot profile a distribution-signed build (it lacks get-task-allow), so for the device
# measurements the owner configures "Apple Development" with a development profile (docs/building.md, "Profiling on a
# device").
set(ERROR_JOURNALING_IOS_SIGNING_IDENTITY "Apple Distribution" CACHE STRING "Code signing identity of a signed iOS build")

# The iOS kit's plugin targets that carry no QT_DEFAULT_PLUGIN property: Qt's plugin auto-linking reads it as an operand
# of $<AND:...>, and an empty value is a generate-time error ("Parameters to $<AND> must resolve to either '0' or '1'").
# Only a plugin with no value is given one, with what the desktop kits say: the ones the application needs are linked,
# the rest are opt-in. (A function, so that the targets exist: find_package(Qt6) made them.) Copied from the snake
# repository, which builds the same kit.
function(ej_ios_default_plugins)
    foreach(plugin IN ITEMS
            QDebugMessageServiceFactoryPlugin QGifPlugin QICOPlugin QIOSIntegrationPlugin QJpegPlugin
            QLocalClientConnectionFactoryPlugin QQmlDebugServerFactoryPlugin QQmlDebuggerServiceFactoryPlugin
            QQmlInspectorServiceFactoryPlugin QQmlNativeDebugConnectorFactoryPlugin QQmlNativeDebugServiceFactoryPlugin
            QQmlPreviewServiceFactoryPlugin QQmlProfilerServiceFactoryPlugin QQuickProfilerAdapterFactoryPlugin
            QSCNetworkReachabilityNetworkInformationPlugin QSecureTransportBackendPlugin QSvgIconPlugin QSvgPlugin
            QTcpServerConnectionFactoryPlugin)
        if(TARGET Qt6::${plugin})
            get_target_property(default Qt6::${plugin} QT_DEFAULT_PLUGIN)
            if(default STREQUAL "default-NOTFOUND")
                set_target_properties(Qt6::${plugin} PROPERTIES QT_DEFAULT_PLUGIN "1")
            endif()
        endif()
    endforeach()
    # Opt-in only: the other integrations, and the permission plugins of services the application does not use (no usage
    # text exists for them in Info.plist, so linking one would crash the first time its check ran).
    foreach(plugin IN ITEMS
            QDarwinBluetoothPermissionPlugin QDarwinCalendarPermissionPlugin QDarwinCameraPermissionPlugin
            QDarwinContactsPermissionPlugin QDarwinLocationPermissionPlugin QDarwinMicrophonePermissionPlugin
            QIosOptionalPlugin_NSPhotoLibraryPlugin QMinimalIntegrationPlugin QOffscreenIntegrationPlugin
            QTlsBackendCertOnlyPlugin QTuioTouchPlugin)
        if(TARGET Qt6::${plugin})
            get_target_property(default Qt6::${plugin} QT_DEFAULT_PLUGIN)
            if(default STREQUAL "default-NOTFOUND")
                set_target_properties(Qt6::${plugin} PROPERTIES QT_DEFAULT_PLUGIN "0")
            endif()
        endif()
    endforeach()
endfunction()

# Makes the application target a program of this device.
function(ej_configure_ios_app target)
    if(ERROR_JOURNALING_IOS_BUILD_NUMBER)
        set(build_number "${ERROR_JOURNALING_IOS_BUILD_NUMBER}")
    else()
        set(build_number "${PROJECT_VERSION}")
    endif()
    set(assets "${ERROR_JOURNALING_PACKAGING_DIR}/ios/Assets.xcassets")
    set_target_properties(${target} PROPERTIES
        MACOSX_BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${ERROR_JOURNALING_PACKAGING_DIR}/ios/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "${ERROR_JOURNALING_DISPLAY_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${ERROR_JOURNALING_APPLICATION_ID}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${build_number}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_COPYRIGHT "Copyright (C) Iain Chesworth Labs contributors"
        # iPhone and iPad.
        XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
        # The default data protection class of the application's files. Xcode applies it when it signs; an unsigned build
        # only carries the reference, and verify_app.sh --signed checks the signature's entitlements.
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${ERROR_JOURNALING_PACKAGING_DIR}/ios/error-journaling.entitlements"
        # The icon set Info.plist.in's CFBundleIconName names; without this Xcode has nothing to compile that slot from and
        # the home screen shows a blank icon.
        XCODE_ATTRIBUTE_ASSETCATALOG_COMPILER_APPICON_NAME "AppIcon")
    # A source path ending in .xcassets is what tells the Xcode generator to compile the catalogue, not copy it.
    target_sources(${target} PRIVATE "${assets}")
    set_source_files_properties("${assets}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    ej_ios_default_plugins()

    # Signing, for a build that is signed. They sign the application target alone: signing every Xcode target forces Xcode
    # to sign static libraries and Qt's resource bundles, which have no bundle identifier and were never meant to be signed.
    if(ERROR_JOURNALING_IOS_SIGNING_TEAM AND ERROR_JOURNALING_IOS_SIGNING_PROFILE)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "YES"
            XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "YES"
            XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Manual"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "${ERROR_JOURNALING_IOS_SIGNING_IDENTITY}"
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${ERROR_JOURNALING_IOS_SIGNING_TEAM}"
            XCODE_ATTRIBUTE_PROVISIONING_PROFILE_SPECIFIER "${ERROR_JOURNALING_IOS_SIGNING_PROFILE}")
    elseif(ERROR_JOURNALING_IOS_SIGNING_TEAM OR ERROR_JOURNALING_IOS_SIGNING_PROFILE)
        message(FATAL_ERROR "ERROR_JOURNALING_IOS_SIGNING_TEAM and ERROR_JOURNALING_IOS_SIGNING_PROFILE go together: one of "
            "them is set and the other is empty. Give both for a signed build, or neither for an unsigned one "
            "(docs/building.md, iOS).")
    endif()
endfunction()
