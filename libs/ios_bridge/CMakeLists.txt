# SPDX-License-Identifier: GPL-3.0-or-later
# What Qt does not cover on an iPhone (the share sheet, haptics), behind interfaces. CMake chooses the implementation,
# not the source: src/platform/ios/ (Objective-C++) for iOS, src/platform/desktop/ (a stub) for everything else.

if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    file(GLOB bridge_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/ios/*.mm")
else()
    file(GLOB bridge_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/desktop/*.cpp")
endif()

add_library(ej_ios_bridge ${bridge_sources})
add_library(error_journaling::ios_bridge ALIAS ej_ios_bridge)

target_include_directories(ej_ios_bridge PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(ej_ios_bridge PUBLIC cxx_std_23)
target_link_libraries(ej_ios_bridge
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
    # Objective-C++ with reference counting, and the frameworks the bridge calls.
    set_source_files_properties(${bridge_sources} PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_link_libraries(ej_ios_bridge PRIVATE "$<LINK_LIBRARY:FRAMEWORK,Foundation>" "$<LINK_LIBRARY:FRAMEWORK,UIKit>")
endif()

# What QML sees of the bridge: a QObject over the interface, so the interface itself needs no Qt.
find_package(Qt6 6.8 REQUIRED COMPONENTS Core)
add_library(ej_qml_bridge
    src/qml_bridge.cpp
    include/error_journaling/qml_bridge.hpp)
add_library(error_journaling::qml_bridge ALIAS ej_qml_bridge)
set_target_properties(ej_qml_bridge PROPERTIES AUTOMOC ON)
target_include_directories(ej_qml_bridge PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_compile_features(ej_qml_bridge PUBLIC cxx_std_23)
target_link_libraries(ej_qml_bridge
    PUBLIC ej_ios_bridge Qt6::Core
    PRIVATE icl::warnings icl::hardening icl::tracy icl::coverage)
icl_relax_generated_warnings(ej_qml_bridge)

if(ERROR_JOURNALING_BUILD_TESTS)
    add_subdirectory(tests)
endif()
