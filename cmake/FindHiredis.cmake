# Copyright (c) 2012-2022 Timo Röhling <timo@gaussglocke.de>
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_search_module(PC_HIREDIS QUIET hiredis)
endif()
find_path(Hiredis_INCLUDE_DIR hiredis.h PATH_SUFFIXES hiredis HINTS ${PC_HIREDIS_INCLUDE_DIRS})
find_library(Hiredis_LIBRARY hiredis HINTS ${PC_HIREDIS_LIBRARY_DIRS})
find_package_handle_standard_args(Hiredis
    FOUND_VAR Hiredis_FOUND
    REQUIRED_VARS
        Hiredis_INCLUDE_DIR
        Hiredis_LIBRARY
)
if(Hiredis_FOUND AND NOT TARGET hiredis::hiredis)
    add_library(hiredis::hiredis UNKNOWN IMPORTED)
    set_target_properties(hiredis::hiredis PROPERTIES
        IMPORTED_LOCATION "${Hiredis_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Hiredis_INCLUDE_DIR}"
    )
endif()
