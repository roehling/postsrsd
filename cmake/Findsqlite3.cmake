# Copyright 2022 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: FSFAP
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_search_module(PC_SQLITE3 QUIET sqlite3)
endif()
find_path(sqlite3_INCLUDE_DIR sqlite3.h HINTS ${PC_SQLITE3_INCLUDE_DIRS})
find_library(sqlite3_LIBRARY sqlite3 HINTS ${PC_SQLITE3_LIBRARY_DIRS})
find_package_handle_standard_args(sqlite3
    FOUND_VAR sqlite3_FOUND
    REQUIRED_VARS
        sqlite3_INCLUDE_DIR
        sqlite3_LIBRARY
)
if(sqlite3_FOUND AND NOT TARGET sqlite3::sqlite3)
    add_library(sqlite3::sqlite3 UNKNOWN IMPORTED)
    set_target_properties(sqlite3::sqlite3 PROPERTIES
        IMPORTED_LOCATION "${sqlite3_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${sqlite3_INCLUDE_DIR}"
    )
endif()
