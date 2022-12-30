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
    pkg_search_module(PC_MILTER QUIET milter)
endif()
find_path(
    LibMilter_INCLUDE_DIR mfapi.h
    PATH_SUFFIXES libmilter
    HINTS ${PC_MILTER_INCLUDE_DIRS}
)
find_library(LibMilter_LIBRARY milter HINTS ${PC_MILTER_LIBRARY_DIRS})
find_package_handle_standard_args(
    LibMilter
    FOUND_VAR LibMilter_FOUND
    REQUIRED_VARS LibMilter_INCLUDE_DIR LibMilter_LIBRARY
)
if(LibMilter_FOUND AND NOT TARGET LibMilter::LibMilter)
    add_library(LibMilter::LibMilter UNKNOWN IMPORTED)
    set_target_properties(
        LibMilter::LibMilter
        PROPERTIES IMPORTED_LOCATION "${LibMilter_LIBRARY}"
                   INTERFACE_INCLUDE_DIRECTORIES "${LibMilter_INCLUDE_DIR}"
    )
endif()
