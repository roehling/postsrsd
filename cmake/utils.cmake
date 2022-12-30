# Copyright 2022 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: FSFAP
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
include(CMakeParseArguments)
include(ExternalProject)
include(FetchContent)

function(add_autotools_dependency name)
    cmake_parse_arguments(arg "" "LIBRARY_NAME;EXPORTED_TARGET" "" ${ARGN})
    FetchContent_MakeAvailable(${name})
    if(NOT TARGET ${arg_EXPORTED_TARGET})
        find_program(MAKE_EXECUTABLE NAMES gmake make mingw32-make REQUIRED)
        set(library_file
            "${CMAKE_STATIC_LIBRARY_PREFIX}${arg_LIBRARY_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}"
        )
        string(TOLOWER "${name}" lc_name)
        ExternalProject_Add(
            Ext${name}
            SOURCE_DIR "${${lc_name}_SOURCE_DIR}"
            UPDATE_DISCONNECTED TRUE
            CONFIGURE_COMMAND <SOURCE_DIR>/configure --disable-shared
                              --prefix=<INSTALL_DIR>
            BUILD_COMMAND ${MAKE_EXECUTABLE} -j
            INSTALL_COMMAND ${MAKE_EXECUTABLE} -j install
            TEST_COMMAND ""
            BUILD_BYPRODUCTS <INSTALL_DIR>/lib/${library_file}
        )
        ExternalProject_Get_Property(Ext${name} INSTALL_DIR)
        add_library(${arg_EXPORTED_TARGET} STATIC IMPORTED)
        set_target_properties(
            ${arg_EXPORTED_TARGET}
            PROPERTIES IMPORTED_LOCATION "${INSTALL_DIR}/lib/${library_file}"
                       INTERFACE_INCLUDE_DIRECTORIES "${INSTALL_DIR}/include"
        )
        add_dependencies(${arg_EXPORTED_TARGET} Ext${name})
        file(MAKE_DIRECTORY "${INSTALL_DIR}/include")
    endif()
endfunction()

function(find_systemd_unit_destination var)
    if(CMAKE_INSTALL_PREFIX MATCHES "^/usr/?$")
        if(EXISTS "/etc/debian_version")
            set(${var}
                "/lib/systemd/system"
                PARENT_SCOPE
            )
        else()
            set(${var}
                "${CMAKE_INSTALL_LIBDIR}/systemd/system"
                PARENT_SCOPE
            )
        endif()
    else()
        set(${var}
            "/etc/systemd/system"
            PARENT_SCOPE
        )
    endif()
endfunction()
