# Copyright 2022-2026 Timo Röhling <timo@gaussglocke.de>
# SPDX-License-Identifier: FSFAP
#
# Copying and distribution of this file, with or without modification, are
# permitted in any medium without royalty provided the copyright notice and
# this notice are preserved. This file is offered as-is, without any warranty.
#
include(FindPackageHandleStandardArgs)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_search_module(PC_LIBCONFUSE QUIET libconfuse)
endif()
find_path(libconfuse_INCLUDE_DIR confuse.h HINTS ${PC_LIBCONFUSE_INCLUDE_DIRS})
find_library(libconfuse_LIBRARY confuse HINTS ${PC_LIBCONFUSE_LIBRARY_DIRS})
find_package_handle_standard_args(
    libconfuse
    FOUND_VAR libconfuse_FOUND
    REQUIRED_VARS libconfuse_INCLUDE_DIR libconfuse_LIBRARY
)
if(libconfuse_FOUND AND NOT TARGET libconfuse::confuse)
    add_library(libconfuse::confuse UNKNOWN IMPORTED)
    set_target_properties(
        libconfuse::confuse
        PROPERTIES IMPORTED_LOCATION "${libconfuse_LIBRARY}"
                   INTERFACE_INCLUDE_DIRECTORIES "${libconfuse_INCLUDE_DIR}"
    )
endif()
