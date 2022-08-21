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
    pkg_search_module(PC_CONFUSE QUIET libconfuse)
endif()
find_path(Confuse_INCLUDE_DIR confuse.h HINTS ${PC_CONFUSE_INCLUDE_DIRS})
find_library(Confuse_LIBRARY confuse HINTS ${PC_CONFUSE_LIBRARY_DIRS})
find_package_handle_standard_args(Confuse
    FOUND_VAR Confuse_FOUND
    REQUIRED_VARS
        Confuse_INCLUDE_DIR
        Confuse_LIBRARY
)
if(Confuse_FOUND AND NOT TARGET Confuse::Confuse)
    add_library(Confuse::Confuse UNKNOWN IMPORTED)
    set_target_properties(Confuse::Confuse PROPERTIES
        IMPORTED_LOCATION "${Confuse_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Confuse_INCLUDE_DIR}"
    )
endif()
