# SPDX-License-Identifier: GPL-3.0-or-later
# EjQtTestEnvironment.cmake - lets a test executable that links Qt find the Qt libraries when it is started by CMake, by
# the build (the discovery that catch_discover_tests runs after linking) and by CTest.
#
# Windows has no rpath: a program finds Qt6Core.dll, Qt6Qml.dll and the others in its own directory or on PATH, and the
# Qt kit's `bin` directory is on neither. Without it the loader stops the program before main() with a modal dialog ("The
# code execution cannot proceed because Qt6Qml.dll was not found"), once for every launch, and nothing in main() can
# prevent that. Everywhere else the kit is found through the rpath CMake writes into the program, so these functions do
# nothing there.
#
#   ej_discover_tests(<target> [catch_discover_tests arguments...])
#       catch_discover_tests with the Qt `bin` directory on PATH for the discovery and for every test it adds.
#   ej_qt_test_environment(<test>...)
#       The same for tests added with add_test. It adds to the properties of the test, so ENVIRONMENT stays as it is set.
#
# Use them from a directory at or below one that has called find_package(Qt6 ...).

include_guard(GLOBAL)

# Sets `${out}` to the Qt `bin` directory in the native form, or to nothing when it is not needed (not Windows) or cannot
# be found (a warning says so, and the dialogs are back).
function(ej_qt_test_bin_dir out)
    set(${out} "" PARENT_SCOPE)
    if(NOT WIN32)
        return()
    endif()

    set(bin_dir "")
    if(QT6_INSTALL_PREFIX AND QT6_INSTALL_BINS)
        # Set by the Qt6 package: where the kit was installed and where it keeps its programs and DLLs.
        cmake_path(ABSOLUTE_PATH QT6_INSTALL_BINS BASE_DIRECTORY "${QT6_INSTALL_PREFIX}" NORMALIZE OUTPUT_VARIABLE bin_dir)
    elseif(TARGET Qt6::Core)
        # Qt6::Core is a DLL in the `bin` directory (the import library is in `lib`).
        foreach(property IN ITEMS IMPORTED_LOCATION IMPORTED_LOCATION_RELEASE IMPORTED_LOCATION_RELWITHDEBINFO
                                  IMPORTED_LOCATION_DEBUG)
            get_target_property(location Qt6::Core ${property})
            if(location)
                get_filename_component(bin_dir "${location}" DIRECTORY)
                break()
            endif()
        endforeach()
    endif()

    if(NOT bin_dir OR NOT IS_DIRECTORY "${bin_dir}")
        message(WARNING "The Qt bin directory was not found: the Qt tests will show a dialog for every missing DLL")
        return()
    endif()
    file(TO_NATIVE_PATH "${bin_dir}" bin_dir)
    set(${out} "${bin_dir}" PARENT_SCOPE)
endfunction()

function(ej_discover_tests target)
    ej_qt_test_bin_dir(bin_dir)
    if(bin_dir)
        catch_discover_tests(${target} ${ARGN} DL_PATHS "${bin_dir}")
    else()
        catch_discover_tests(${target} ${ARGN})
    endif()
endfunction()

function(ej_qt_test_environment)
    ej_qt_test_bin_dir(bin_dir)
    if(bin_dir)
        set_property(TEST ${ARGN} APPEND PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${bin_dir}")
    endif()
endfunction()
