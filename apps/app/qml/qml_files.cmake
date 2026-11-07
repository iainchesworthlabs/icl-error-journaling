# SPDX-License-Identifier: GPL-3.0-or-later
# The QML files of the module ErrorJournalingApp (docs/plan/stage-4.md, "Scope and conventions"). The application and the
# UI tests each build the module from the same files, because an executable's resources cannot be linked into another
# executable. A work package adds its .qml files under qml/ and neither CMakeLists.txt changes: CONFIGURE_DEPENDS runs the
# glob again at build time.
#
#   ej_collect_qml_files(<variable>)
#
# sets <variable> to every .qml file under this directory, and, in the directory scope of the caller, marks the singletons
# (a `pragma Singleton` file needs QT_QML_SINGLETON_TYPE on its source file in the directory that creates the module) and
# gives each file the resource path qml/<relative path>, whatever directory the module is created in.
#
# What differs between the phone and a desktop is in qml/platform/<kind>/, the way C++ keeps it in src/platform/<os>/
# (AGENTS.md): `ios` for an iOS build, `desktop` for every other, and the same file names in both. A file of the other
# kind is left out, so the iOS build never loads the desktop file dialog (Stage 4, decision 17).
function(ej_collect_qml_files variable)
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")
    file(GLOB_RECURSE files CONFIGURE_DEPENDS "${root}/*.qml")
    list(SORT files)
    if(IOS)
        set(other_platform "desktop")
    else()
        set(other_platform "ios")
    endif()
    set(kept "")
    foreach(file IN LISTS files)
        file(RELATIVE_PATH relative "${root}" "${file}")
        if(relative MATCHES "^platform/${other_platform}/")
            continue()
        endif()
        list(APPEND kept "${file}")
        set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "qml/${relative}")
        file(STRINGS "${file}" pragma LIMIT_COUNT 1 REGEX "^pragma Singleton")
        if(pragma)
            set_source_files_properties("${file}" PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
        endif()
    endforeach()
    set(${variable} ${kept} PARENT_SCOPE)
endfunction()
